/*
 * SPDX-FileCopyrightText: 2014 Emmanuel Pescosta <emmanuelpescosta099@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dolphintabwidget.h"
#include "shellkit.h"

#include "dolphin_generalsettings.h"
#include "dolphintabbar.h"
#include "dolphinviewcontainer.h"
#include "views/draganddrophelper.h"

#include <KAcceleratorManager>
#include <KConfigGroup>
#include <KIO/CommandLauncherJob>
#include <KLocalizedString>
#include <KShell>
#include <KStringHandler>
#include <kio/global.h>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDropEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QStackedWidget>
#include <QToolButton>
#include <QWindow>

namespace
{
/**
 * shellkit fork: a Windows 11 caption button (minimize, maximize/restore, close) for the tab
 * row, which stands in for the title bar when the shellkit window frame hides its own.
 */
class CaptionButton : public QAbstractButton
{
public:
    enum Kind { Minimize, Maximize, Close };

    static constexpr int Width = 46;
    // Windows 7: a glassy button group hanging from the top edge, close wider and red.
    static constexpr int Win7Height = 20;

    int buttonWidth() const
    {
        if (!shellkitWin7()) {
            return Width;
        }
        return m_kind == Close ? 45 : 27;
    }

    CaptionButton(Kind kind, QWidget *parent)
        : QAbstractButton(parent)
        , m_kind(kind)
    {
        setAttribute(Qt::WA_Hover);
        setFocusPolicy(Qt::NoFocus);
        connect(this, &QAbstractButton::clicked, this, [this]() {
            QWidget *top = window();
            switch (m_kind) {
            case Minimize:
                top->showMinimized();
                break;
            case Maximize:
                top->isMaximized() ? top->showNormal() : top->showMaximized();
                break;
            case Close:
                top->close();
                break;
            }
        });
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (shellkitWin7()) {
            paintWin7(painter);
            return;
        }
        const bool dark = palette().color(QPalette::Window).lightness() < 128;
        QColor glyph = palette().color(window()->isActiveWindow() ? QPalette::Active : QPalette::Inactive, QPalette::WindowText);
        if (!window()->isActiveWindow()) {
            glyph.setAlphaF(0.5);
        }
        const bool hot = underMouse() || isDown();
        if (m_kind == Close && hot) {
            // The system critical red, slightly see-through while pressed.
            painter.fillRect(rect(), QColor(0xc4, 0x2b, 0x1c, isDown() ? 0xe6 : 0xff));
            glyph = Qt::white;
        } else if (hot) {
            // WinUI SubtleFillColorSecondary on hover, SubtleFillColorTertiary while pressed.
            QColor fill = dark ? Qt::white : Qt::black;
            fill.setAlpha(dark ? (isDown() ? 0x0a : 0x0f) : (isDown() ? 0x06 : 0x09));
            painter.fillRect(rect(), fill);
        }

        QPen pen(glyph, 1.0);
        pen.setCapStyle(Qt::FlatCap);
        pen.setJoinStyle(Qt::MiterJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        // The same 10 px glyphs as the window frame's caption buttons, snapped to whole pixels.
        const qreal s = 10;
        const qreal x = qRound(width() / 2.0 - s / 2);
        const qreal y = qRound(height() / 2.0 - s / 2);
        const qreal r = 1.5;
        switch (m_kind) {
        case Minimize:
            painter.drawLine(QPointF(x, y + s / 2 - 0.5), QPointF(x + s, y + s / 2 - 0.5));
            break;
        case Maximize:
            if (window()->isMaximized()) {
                const qreal left = x + 0.5, top = y + 0.5, right = x + s - 0.5, bottom = y + s - 0.5;
                painter.drawRoundedRect(QRectF(left, top + 2, s - 3, s - 3), r, r);
                QPainterPath back;
                back.moveTo(left + 2, top + 2);
                back.lineTo(left + 2, top + r);
                back.quadTo(left + 2, top, left + 2 + r, top);
                back.lineTo(right - r, top);
                back.quadTo(right, top, right, top + r);
                back.lineTo(right, bottom - 2 - r);
                back.quadTo(right, bottom - 2, right - r, bottom - 2);
                back.lineTo(right - 2, bottom - 2);
                painter.drawPath(back);
            } else {
                painter.drawRoundedRect(QRectF(x + 0.5, y + 0.5, s - 1, s - 1), r, r);
            }
            break;
        case Close:
            painter.drawLine(QPointF(x, y), QPointF(x + s, y + s));
            painter.drawLine(QPointF(x + s, y), QPointF(x, y + s));
            break;
        }
    }

private:
    void paintWin7(QPainter &painter)
    {
        const bool dark = palette().color(QPalette::Window).lightness() < 128;
        const bool active = window()->isActiveWindow();
        const bool hot = underMouse() || isDown();
        // The group's outer corners are rounded: bottom left of Minimize, bottom right of Close.
        const QRectF r = QRectF(rect()).adjusted(0.5, -0.5, -0.5, -0.5);
        const qreal radius = 4;
        QPainterPath shape;
        shape.moveTo(r.topLeft());
        shape.lineTo(r.topRight());
        if (m_kind == Close) {
            shape.lineTo(r.right(), r.bottom() - radius);
            shape.quadTo(r.bottomRight(), QPointF(r.right() - radius, r.bottom()));
        } else {
            shape.lineTo(r.bottomRight());
        }
        if (m_kind == Minimize) {
            shape.lineTo(r.left() + radius, r.bottom());
            shape.quadTo(r.bottomLeft(), QPointF(r.left(), r.bottom() - radius));
        } else {
            shape.lineTo(r.bottomLeft());
        }
        shape.closeSubpath();

        QLinearGradient fill(r.topLeft(), r.bottomLeft());
        if (m_kind == Close && (active || hot)) {
            fill.setColorAt(0, hot ? QColor(0xf5, 0xa8, 0x98) : QColor(0xe8, 0xa4, 0x93));
            fill.setColorAt(0.45, hot ? QColor(0xe8, 0x56, 0x44) : QColor(0xd0, 0x5e, 0x4e));
            fill.setColorAt(1, isDown() ? QColor(0x9a, 0x26, 0x18) : hot ? QColor(0xd6, 0x3c, 0x26) : QColor(0xb8, 0x3d, 0x2d));
        } else if (dark) {
            fill.setColorAt(0, hot ? QColor(0x5d, 0x7a, 0x9c) : QColor(0x46, 0x55, 0x68));
            fill.setColorAt(1, hot ? QColor(0x2f, 0x5c, 0x8e) : QColor(0x2a, 0x33, 0x3f));
        } else {
            fill.setColorAt(0, hot ? QColor(0xe4, 0xf3, 0xff) : QColor(0xe9, 0xf0, 0xf8));
            fill.setColorAt(0.45, hot ? QColor(0xa9, 0xd3, 0xf6) : QColor(0xcf, 0xdc, 0xeb));
            fill.setColorAt(1, isDown() ? QColor(0x3d, 0x7f, 0xc4) : hot ? QColor(0x6c, 0xb2, 0xeb) : QColor(0xb4, 0xc6, 0xdc));
        }
        painter.setPen(QPen(dark ? QColor(0x10, 0x14, 0x1a) : QColor(0x4f, 0x63, 0x7c, active ? 0xff : 0x99), 1.0));
        painter.setBrush(fill);
        painter.drawPath(shape);

        // White glyphs with a dark outline, as on the Aero caption buttons.
        QPainterPath glyph;
        const QPointF c(qRound(width() / 2.0), qRound(height() / 2.0));
        switch (m_kind) {
        case Minimize:
            glyph.addRect(QRectF(c.x() - 4.5, c.y() + 1.5, 9, 3));
            break;
        case Maximize:
            // A frame with a thicker top edge.
            glyph.setFillRule(Qt::OddEvenFill);
            glyph.addRect(QRectF(c.x() - 5, c.y() - 4, 10, 8));
            glyph.addRect(QRectF(c.x() - 3, c.y() - 1, 6, 3));
            break;
        case Close: {
            QPainterPathStroker stroker;
            stroker.setWidth(2.6);
            stroker.setCapStyle(Qt::FlatCap);
            QPainterPath cross;
            cross.moveTo(c.x() - 4, c.y() - 3.5);
            cross.lineTo(c.x() + 4, c.y() + 3.5);
            cross.moveTo(c.x() + 4, c.y() - 3.5);
            cross.lineTo(c.x() - 4, c.y() + 3.5);
            glyph = stroker.createStroke(cross).simplified();
            break;
        }
        }
        painter.setPen(QPen(QColor(0x1d, 0x26, 0x33, active ? 0xcc : 0x77), 1.0));
        painter.setBrush(QColor(255, 255, 255, active ? 0xff : 0xaa));
        painter.drawPath(glyph);
    }

    Kind m_kind;
};

/**
 * shellkit fork: holds the tab bar outside the tab widget. QTabWidget keeps laying out its
 * tab bar and page stack as if the bar were on top of the pages; this widget puts the bar
 * back into the row and gives the stack the whole tab widget.
 */
class TabRow : public QWidget
{
public:
    TabRow(QTabBar *tabBar, QWidget *stack, QAction *newTabAction, QWidget *parent)
        : QWidget(parent)
        , m_tabBar(tabBar)
        , m_stack(stack)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        m_newTab = new QToolButton(this);
        m_newTab->setAutoRaise(true);
        m_newTab->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
        m_newTab->setToolTip(newTabAction->text().remove(QLatin1Char('&')));
        m_newTab->setFocusPolicy(Qt::NoFocus);
        connect(m_newTab, &QToolButton::clicked, newTabAction, &QAction::trigger);

        // With the shellkit window frame the row is the title bar: caption buttons on the
        // right, and the free space moves and maximizes the window.
        if (shellkitIntegratedCaption()) {
            for (auto kind : {CaptionButton::Minimize, CaptionButton::Maximize, CaptionButton::Close}) {
                m_captionButtons.append(new CaptionButton(kind, this));
            }
        }

        m_tabBar->setParent(this);
        m_tabBar->show();
        m_tabBar->installEventFilter(this);
        m_stack->installEventFilter(this);
    }

    QSize sizeHint() const override
    {
        return QSize(m_tabBar->sizeHint().width() + ButtonSpace + captionWidth(), rowHeight());
    }

    QSize minimumSizeHint() const override
    {
        return QSize(ButtonSpace + captionWidth(), rowHeight());
    }

    /** Windows 7 has no tabs: the row shows them only once a second tab is open. */
    void updateTabsShown()
    {
        updateGeometry();
        place();
    }

protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::LayoutRequest) {
            place();
        } else if (event->type() == QEvent::Show && !m_captionButtons.isEmpty() && m_watchedWindow != window()) {
            // Repaint the maximize glyph and the inactive colour with the window state.
            m_watchedWindow = window();
            m_watchedWindow->installEventFilter(this);
        }
        return QWidget::event(event);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        place();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        m_pressPos = event->button() == Qt::LeftButton && !m_captionButtons.isEmpty() ? event->position().toPoint() : QPoint(-1, -1);
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        // Start the move only once the pointer travels, so a double click still maximizes.
        if (m_pressPos.x() >= 0 && (event->buttons() & Qt::LeftButton)
            && (event->position().toPoint() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
            m_pressPos = QPoint(-1, -1);
            if (QWindow *handle = window()->windowHandle()) {
                handle->startSystemMove();
            }
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        m_pressPos = QPoint(-1, -1);
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && !m_captionButtons.isEmpty()) {
            QWidget *top = window();
            top->isMaximized() ? top->showNormal() : top->showMaximized();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_watchedWindow) {
            if (event->type() == QEvent::WindowStateChange || event->type() == QEvent::ActivationChange) {
                for (QWidget *button : std::as_const(m_captionButtons)) {
                    button->update();
                }
            }
            return QWidget::eventFilter(watched, event);
        }
        if (event->type() == QEvent::Move || event->type() == QEvent::Resize) {
            if (watched == m_tabBar) {
                if (m_tabBar->geometry() != tabBarRect()) {
                    place();
                }
            } else if (watched == m_stack && m_stack->parentWidget()) {
                const QRect whole = m_stack->parentWidget()->rect();
                if (m_stack->geometry() != whole) {
                    m_stack->setGeometry(whole);
                }
            }
        }
        return QWidget::eventFilter(watched, event);
    }

private:
    static constexpr int ButtonSpace = 44;
    // Windows 7 without tabs: a strip of glass above the address row for the caption buttons.
    static constexpr int Win7RowHeight = 26;
    static constexpr int Win7RightMargin = 6;

    bool tabsShown() const
    {
        return !shellkitWin7() || m_tabBar->count() > 1;
    }

    int rowHeight() const
    {
        if (tabsShown()) {
            return m_tabBar->sizeHint().height();
        }
        return m_captionButtons.isEmpty() ? 0 : Win7RowHeight;
    }

    int captionWidth() const
    {
        int width = shellkitWin7() && !m_captionButtons.isEmpty() ? Win7RightMargin : 0;
        for (const CaptionButton *button : m_captionButtons) {
            width += button->buttonWidth();
        }
        return width;
    }

    QRect tabBarRect() const
    {
        const int width = qMin(m_tabBar->sizeHint().width(), qMax(0, this->width() - ButtonSpace - captionWidth()));
        return QRect(0, 0, width, height());
    }

    void place()
    {
        m_tabBar->setVisible(tabsShown());
        m_newTab->setVisible(tabsShown());
        const QRect rect = tabBarRect();
        if (m_tabBar->geometry() != rect) {
            m_tabBar->setGeometry(rect);
        }
        const int count = m_tabBar->count();
        const int right = count > 0 ? m_tabBar->tabRect(count - 1).right() + 1 : 0;
        const QSize button(32, 32);
        // Vertically centred on the tab shapes, which leave a gap above them.
        m_newTab->setGeometry(qMin(right, rect.width()) + 6, (height() + 6 - button.height()) / 2, button.width(), button.height());
        int x = width() - captionWidth();
        for (CaptionButton *caption : std::as_const(m_captionButtons)) {
            caption->setGeometry(x, 0, caption->buttonWidth(), shellkitWin7() ? CaptionButton::Win7Height : height());
            x += caption->buttonWidth();
        }
    }

    QTabBar *m_tabBar;
    QWidget *m_stack;
    QToolButton *m_newTab;
    QList<CaptionButton *> m_captionButtons;
    QPointer<QWidget> m_watchedWindow;
    QPoint m_pressPos = QPoint(-1, -1);
};
}

DolphinTabWidget::DolphinTabWidget(DolphinNavigatorsWidgetAction *navigatorsWidget, QWidget *parent)
    : QTabWidget(parent)
    , m_lastViewedTab(nullptr)
    , m_navigatorsWidget{navigatorsWidget}
{
    KAcceleratorManager::setNoAccel(this);

    connect(this, &DolphinTabWidget::tabCloseRequested, this, QOverload<int>::of(&DolphinTabWidget::closeTab));
    connect(this, &DolphinTabWidget::currentChanged, this, &DolphinTabWidget::currentTabChanged);

    DolphinTabBar *tabBar = new DolphinTabBar(this);
    connect(tabBar, &DolphinTabBar::openNewActivatedTab, this, QOverload<int>::of(&DolphinTabWidget::openNewActivatedTab));
    connect(tabBar, &DolphinTabBar::tabDragMoveEvent, this, &DolphinTabWidget::tabDragMoveEvent);
    connect(tabBar, &DolphinTabBar::tabDropEvent, this, &DolphinTabWidget::tabDropEvent);
    connect(tabBar, &DolphinTabBar::tabDetachRequested, this, &DolphinTabWidget::detachTab);
    connect(tabBar, &DolphinTabBar::tabRenamed, this, &DolphinTabWidget::renameTab);

    setTabBar(tabBar);
    setDocumentMode(true);
    setElideMode(Qt::ElideRight);
    setUsesScrollButtons(true);
    // shellkit fork: Windows 11 always shows the tabs, each with its close button.
    setTabBarAutoHide(false);
    tabBar->setTabsClosable(true);

    auto stackWidget{findChild<QStackedWidget *>()};
    // i18n: This accessible name will be announced any time the user moves keyboard focus e.g. from the toolbar or the places panel towards the main working
    // area of Dolphin. It gives structure. This container does not only contain the main view but also the status bar, the search panel, filter, and selection
    // mode bars, so calling it just a "View" is a bit wrong, but hopefully still gets the point across.
    stackWidget->setAccessibleName(i18nc("accessible name of Dolphin's view container", "Location View")); // Without this call, the non-descript Qt provided
                                                                                                           // "Layered Pane" role is announced.
}

QWidget *DolphinTabWidget::createTabRow(QAction *newTabAction)
{
    auto row = new TabRow(tabBar(), findChild<QStackedWidget *>(QString(), Qt::FindDirectChildrenOnly), newTabAction, nullptr);
    connect(this, &DolphinTabWidget::tabCountChanged, row, &TabRow::updateTabsShown);
    return row;
}

DolphinTabPage *DolphinTabWidget::currentTabPage() const
{
    return tabPageAt(currentIndex());
}

DolphinTabPage *DolphinTabWidget::nextTabPage() const
{
    const int index = currentIndex() + 1;
    return tabPageAt(index < count() ? index : 0);
}

DolphinTabPage *DolphinTabWidget::prevTabPage() const
{
    const int index = currentIndex() - 1;
    return tabPageAt(index >= 0 ? index : (count() - 1));
}

DolphinTabPage *DolphinTabWidget::tabPageAt(const int index) const
{
    return static_cast<DolphinTabPage *>(widget(index));
}

void DolphinTabWidget::saveProperties(KConfigGroup &group) const
{
    const int tabCount = count();
    group.writeEntry("Tab Count", tabCount);
    group.writeEntry("Active Tab Index", currentIndex());

    for (int i = 0; i < tabCount; ++i) {
        const DolphinTabPage *tabPage = tabPageAt(i);
        group.writeEntry("Tab Data " % QString::number(i), tabPage->saveState());
    }
}

void DolphinTabWidget::readProperties(const KConfigGroup &group)
{
    const int tabCount = group.readEntry("Tab Count", 0);
    for (int i = 0; i < tabCount; ++i) {
        if (i >= count()) {
            openNewActivatedTab();
        }
        const QByteArray state = group.readEntry("Tab Data " % QString::number(i), QByteArray());
        setCurrentIndex(i);
        tabPageAt(i)->restoreState(state);
    }

    const int index = group.readEntry("Active Tab Index", 0);
    setCurrentIndex(index);
}

void DolphinTabWidget::refreshViews()
{
    // Left-elision is better when showing full paths, since you care most
    // about the current directory which is on the right
    if (GeneralSettings::showFullPathInTitlebar()) {
        setElideMode(Qt::ElideLeft);
    } else {
        setElideMode(Qt::ElideRight);
    }

    const int tabCount = count();
    for (int i = 0; i < tabCount; ++i) {
        updateTabName(i);
        tabPageAt(i)->refreshViews();
    }
}

void DolphinTabWidget::updateTabName(int index)
{
    Q_ASSERT(index >= 0);

    if (!tabPageAt(index)->customLabel().isEmpty()) {
        QString name = tabPageAt(index)->customLabel();
        tabBar()->setTabText(index, name);
        return;
    }

    tabBar()->setTabText(index, tabName(tabPageAt(index)));
}

bool DolphinTabWidget::isUrlOpen(const QUrl &url) const
{
    return viewOpenAtDirectory(url).has_value();
}

bool DolphinTabWidget::isItemVisibleInAnyView(const QUrl &urlOfItem) const
{
    return viewShowingItem(urlOfItem).has_value();
}

void DolphinTabWidget::openNewActivatedTab()
{
    std::unique_ptr<DolphinUrlNavigator::VisualState> oldNavigatorState;
    if (currentTabPage()->primaryViewActive() || !m_navigatorsWidget->secondaryUrlNavigator()) {
        oldNavigatorState = m_navigatorsWidget->primaryUrlNavigator()->visualState();
    } else {
        oldNavigatorState = m_navigatorsWidget->secondaryUrlNavigator()->visualState();
    }

    // shellkit fork: a new tab opens the Home page, as in Windows (shellkitStartUrl()).
    openNewActivatedTab(shellkitStartUrl());

    DolphinViewContainer *newActiveViewContainer = currentTabPage()->activeViewContainer();
    Q_ASSERT(newActiveViewContainer);

    // The URL navigator of the new tab should have the same editable state
    // as the current tab
    newActiveViewContainer->urlNavigator()->setVisualState(*oldNavigatorState.get());

    // Always focus the new tab's view
    newActiveViewContainer->view()->setFocus();
}

void DolphinTabWidget::openNewActivatedTab(const QUrl &primaryUrl, const QUrl &secondaryUrl)
{
    DolphinTabPage *tabPage = openNewTab(primaryUrl, secondaryUrl);
    Q_EMIT activeViewChanged(tabPage->activeViewContainer());

    if (GeneralSettings::openNewTabAfterLastTab()) {
        setCurrentIndex(count() - 1);
    } else {
        setCurrentIndex(currentIndex() + 1);
    }
}

DolphinTabPage *DolphinTabWidget::openNewTab(const QUrl &primaryUrl, const QUrl &secondaryUrl, DolphinTabWidget::NewTabPosition position)
{
    QWidget *focusWidget = QApplication::focusWidget();

    DolphinTabPage *tabPage = new DolphinTabPage(primaryUrl, secondaryUrl, this);
    tabPage->setActive(false);
    connect(tabPage, &DolphinTabPage::activeViewChanged, this, &DolphinTabWidget::activeViewChanged);
    connect(tabPage, &DolphinTabPage::activeViewUrlChanged, this, &DolphinTabWidget::tabUrlChanged);
    connect(tabPage->activeViewContainer(), &DolphinViewContainer::captionChanged, this, [this, tabPage]() {
        updateTabName(indexOf(tabPage));
    });

    if (position == NewTabPosition::FollowSetting) {
        if (GeneralSettings::openNewTabAfterLastTab()) {
            position = NewTabPosition::AtEnd;
        } else {
            position = NewTabPosition::AfterCurrent;
        }
    }

    int newTabIndex = -1;
    if (position == NewTabPosition::AfterCurrent || (position == NewTabPosition::FollowSetting && !GeneralSettings::openNewTabAfterLastTab())) {
        newTabIndex = currentIndex() + 1;
    }

    insertTab(newTabIndex, tabPage, QIcon() /* loaded in tabInserted */, tabName(tabPage));

    if (focusWidget) {
        // The DolphinViewContainer grabbed the keyboard focus. As the tab is opened
        // in background, assure that the previous focused widget gets the focus back.
        focusWidget->setFocus();
    }
    return tabPage;
}

void DolphinTabWidget::openDirectories(const QList<QUrl> &dirs, bool splitView)
{
    Q_ASSERT(dirs.size() > 0);

    bool somethingWasAlreadyOpen = false;

    QList<QUrl>::const_iterator it = dirs.constBegin();
    while (it != dirs.constEnd()) {
        const QUrl &primaryUrl = *(it++);
        const std::optional<ViewIndex> viewIndexAtDirectory = viewOpenAtDirectory(primaryUrl);

        // When the user asks for a URL that's already open,
        // activate it instead of opening a new tab
        if (viewIndexAtDirectory.has_value()) {
            somethingWasAlreadyOpen = true;
            activateViewContainerAt(viewIndexAtDirectory.value());
        } else if (splitView && (it != dirs.constEnd())) {
            const QUrl &secondaryUrl = *(it++);
            if (somethingWasAlreadyOpen) {
                openNewTab(primaryUrl, secondaryUrl);
            } else {
                openNewActivatedTab(primaryUrl, secondaryUrl);
            }
        } else {
            if (somethingWasAlreadyOpen) {
                openNewTab(primaryUrl);
            } else {
                openNewActivatedTab(primaryUrl);
            }
        }
    }
}

void DolphinTabWidget::openFiles(const QList<QUrl> &files, bool splitView)
{
    Q_ASSERT(files.size() > 0);

    // Get all distinct directories from 'files'.
    QList<QUrl> dirsThatNeedToBeOpened;
    QList<QUrl> dirsThatWereAlreadyOpen;
    for (const QUrl &file : files) {
        const QUrl dir(file.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash));
        if (dirsThatNeedToBeOpened.contains(dir) || dirsThatWereAlreadyOpen.contains(dir)) {
            continue;
        }

        // The selecting of files that we do later will not work in views that already have items selected.
        // So we check if dir is already open and clear the selection if it is. BUG: 417230
        // We also make sure the view will be activated.
        auto viewIndex = viewShowingItem(file);
        if (viewIndex.has_value()) {
            viewContainerAt(viewIndex.value())->view()->clearSelection();
            activateViewContainerAt(viewIndex.value());
            dirsThatWereAlreadyOpen.append(dir);
        } else {
            dirsThatNeedToBeOpened.append(dir);
        }
    }

    const int oldTabCount = count();
    // Open a tab for each directory. If the "split view" option is enabled,
    // two directories are shown inside one tab (see openDirectories()).
    if (dirsThatNeedToBeOpened.size() > 0) {
        openDirectories(dirsThatNeedToBeOpened, splitView);
    }
    const int tabCount = count();

    // Select the files. Although the files can be split between several
    // tabs, there is no need to split 'files' accordingly, as
    // the DolphinView will just ignore invalid selections.
    for (int i = 0; i < tabCount; ++i) {
        DolphinTabPage *tabPage = tabPageAt(i);
        tabPage->markUrlsAsSelected(files);
        tabPage->markUrlAsCurrent(files.first());
        if (i < oldTabCount) {
            // Force selection of file if directory was already open, BUG: 417230
            tabPage->activeViewContainer()->view()->updateViewState();
        }
    }
}

void DolphinTabWidget::closeTab()
{
    closeTab(currentIndex());
}

void DolphinTabWidget::closeTab(const int index)
{
    Q_ASSERT(index >= 0);
    Q_ASSERT(index < count());

    if (count() < 2) {
        // Close Dolphin when closing the last tab.
        parentWidget()->close();
        return;
    }

    DolphinTabPage *tabPage = tabPageAt(index);
    Q_EMIT rememberClosedTab(tabPage->activeViewContainer()->url(), tabPage->saveState());

    removeTab(index);
    tabPage->deleteLater();
}

void DolphinTabWidget::activateTab(const int index)
{
    if (index < count()) {
        setCurrentIndex(index);
    }
}

void DolphinTabWidget::activateLastTab()
{
    setCurrentIndex(count() - 1);
}

void DolphinTabWidget::activateNextTab()
{
    const int index = currentIndex() + 1;
    setCurrentIndex(index < count() ? index : 0);
}

void DolphinTabWidget::activatePrevTab()
{
    const int index = currentIndex() - 1;
    setCurrentIndex(index >= 0 ? index : (count() - 1));
}

void DolphinTabWidget::restoreClosedTab(const QByteArray &state)
{
    openNewActivatedTab();
    currentTabPage()->restoreState(state);
}

void DolphinTabWidget::copyToInactiveSplitView()
{
    const DolphinTabPage *tabPage = currentTabPage();
    if (!tabPage->splitViewEnabled()) {
        return;
    }

    const KFileItemList selectedItems = tabPage->activeViewContainer()->view()->selectedItems();
    if (selectedItems.isEmpty()) {
        return;
    }

    DolphinView *const inactiveView = tabPage->inactiveViewContainer()->view();
    inactiveView->copySelectedItems(selectedItems, inactiveView->url());
}

void DolphinTabWidget::moveToInactiveSplitView()
{
    const DolphinTabPage *tabPage = currentTabPage();
    if (!tabPage->splitViewEnabled()) {
        return;
    }

    const KFileItemList selectedItems = tabPage->activeViewContainer()->view()->selectedItems();
    if (selectedItems.isEmpty()) {
        return;
    }

    DolphinView *const inactiveView = tabPage->inactiveViewContainer()->view();
    inactiveView->moveSelectedItems(selectedItems, inactiveView->url());
}

void DolphinTabWidget::focusInactiveSplitView()
{
    DolphinTabPage *const tabPage = currentTabPage();
    if (!tabPage->splitViewEnabled()) {
        tabPage->setSplitViewEnabled(true, WithAnimation, tabPage->activeViewContainer()->url());
    } else {
        activateViewContainerAt(ViewIndex{currentIndex(), !tabPage->primaryViewActive()});
    }
}

void DolphinTabWidget::detachTab(int index)
{
    Q_ASSERT(index >= 0);

    QStringList args;

    const DolphinTabPage *tabPage = tabPageAt(index);
    args << tabPage->primaryViewContainer()->url().url();
    if (tabPage->splitViewEnabled()) {
        args << tabPage->secondaryViewContainer()->url().url();
        args << QStringLiteral("--split");
    }
    args << QStringLiteral("--new-window");

    KIO::CommandLauncherJob *job = new KIO::CommandLauncherJob("dolphin", args, this);
    job->setDesktopName(QStringLiteral("org.shellkit.files"));
    job->start();

    closeTab(index);
}

void DolphinTabWidget::openNewActivatedTab(int index)
{
    Q_ASSERT(index >= 0);
    const DolphinTabPage *tabPage = tabPageAt(index);
    openNewActivatedTab(tabPage->activeViewContainer()->url());
}

void DolphinTabWidget::tabDragMoveEvent(int index, QDragMoveEvent *event)
{
    if (index >= 0) {
        DolphinView *view = tabPageAt(index)->activeViewContainer()->view();
        DragAndDropHelper::updateDropAction(event, view->url());
    }
}

void DolphinTabWidget::tabDropEvent(int index, QDropEvent *event)
{
    if (index >= 0) {
        DolphinView *view = tabPageAt(index)->activeViewContainer()->view();
        view->dropUrls(view->url(), event, view);
    } else {
        const auto urls = event->mimeData()->urls();

        for (const QUrl &url : urls) {
            auto *job = KIO::stat(url, KIO::StatJob::SourceSide, KIO::StatDetail::StatBasic, KIO::JobFlag::HideProgressInfo);
            connect(job, &KJob::result, this, [this, job]() {
                if (!job->error() && job->statResult().isDir()) {
                    openNewTab(job->url(), QUrl(), NewTabPosition::AtEnd);
                }
            });
        }
    }
}

void DolphinTabWidget::tabUrlChanged(const QUrl &url)
{
    const int index = indexOf(qobject_cast<QWidget *>(sender()));
    if (index >= 0) {
        updateTabName(index);
        tabBar()->setTabToolTip(index, url.toDisplayString(QUrl::PreferLocalFile));
        // shellkit fork: the tab bar is always shown, so the icon is set right away.
        tabBar()->setTabIcon(index, tabIcon(url));

        // Emit the currentUrlChanged signal if the url of the current tab has been changed.
        if (index == currentIndex()) {
            Q_EMIT currentUrlChanged(url);
        }

        Q_EMIT urlChanged(url);
    }
}

void DolphinTabWidget::currentTabChanged(int index)
{
    DolphinTabPage *tabPage = tabPageAt(index);
    if (tabPage == m_lastViewedTab) {
        return;
    }
    if (m_lastViewedTab) {
        m_lastViewedTab->disconnectNavigators();
        m_lastViewedTab->setActive(false);
    }
    if (tabPage->splitViewEnabled() && !m_navigatorsWidget->secondaryUrlNavigator()) {
        m_navigatorsWidget->createSecondaryUrlNavigator();
    }
    DolphinViewContainer *viewContainer = tabPage->activeViewContainer();
    Q_EMIT activeViewChanged(viewContainer);
    Q_EMIT currentUrlChanged(viewContainer->url());
    tabPage->setActive(true);
    tabPage->connectNavigators(m_navigatorsWidget);
    m_navigatorsWidget->setSecondaryNavigatorVisible(tabPage->splitViewEnabled());
    m_lastViewedTab = tabPage;
}

void DolphinTabWidget::renameTab(int index, const QString &name)
{
    tabPageAt(index)->setCustomLabel(name);
    updateTabName(index);
}

QIcon DolphinTabWidget::tabIcon(const QUrl &url)
{
    if (isShellkitHomeUrl(url)) {
        return QIcon::fromTheme(QStringLiteral("user-home"));
    }
    if (url.scheme() == QLatin1String("shellkit-thispc")) {
        return QIcon::fromTheme(QStringLiteral("computer"));
    }
    if (url.scheme() == QLatin1String("shellkit-network")) {
        return QIcon::fromTheme(QStringLiteral("network-workgroup"));
    }
    // ensure the path url ends with a slash to have proper folder icon for remote folders
    const QUrl pathUrl = QUrl(url.adjusted(QUrl::StripTrailingSlash).toString(QUrl::FullyEncoded).append("/"));
    return QIcon::fromTheme(KIO::iconNameForUrl(pathUrl));
}

void DolphinTabWidget::tabInserted(int index)
{
    QTabWidget::tabInserted(index);

    // shellkit fork: the tab bar is always shown, no need to wait for it.
    {
        // Resolve all pending tab icons
        for (int i = 0; i < count(); ++i) {
            const QUrl url = tabPageAt(i)->activeViewContainer()->url();
            if (tabBar()->tabIcon(i).isNull()) {
                tabBar()->setTabIcon(i, tabIcon(url));
            }
            if (tabBar()->tabToolTip(i).isEmpty()) {
                tabBar()->setTabToolTip(i, url.toDisplayString(QUrl::PreferLocalFile));
            }
        }
    }

    Q_EMIT tabCountChanged(count());
}

void DolphinTabWidget::tabRemoved(int index)
{
    QTabWidget::tabRemoved(index);

    Q_EMIT tabCountChanged(count());
    m_lastViewedTab->setActive(true);
}

QString DolphinTabWidget::tabName(DolphinTabPage *tabPage) const
{
    if (!tabPage) {
        return QString();
    }
    // clang-format off
    QString name;
    if (tabPage->splitViewEnabled()) {
        if (tabPage->primaryViewActive()) {
            // i18n: %1 is the primary view and %2 the secondary view. For left to right languages the primary view is on the left so we also want it to be on the
            // left in the tab name. In right to left languages the primary view would be on the right so the tab name should match.
            name = i18nc("@title:tab Active primary view | (Inactive secondary view)", "%1 | (%2)", tabPage->primaryViewContainer()->caption(), tabPage->secondaryViewContainer()->caption());
        } else {
            // i18n: %1 is the primary view and %2 the secondary view. For left to right languages the primary view is on the left so we also want it to be on the
            // left in the tab name. In right to left languages the primary view would be on the right so the tab name should match.
            name = i18nc("@title:tab (Inactive primary view) | Active secondary view", "(%1) | %2", tabPage->primaryViewContainer()->caption(), tabPage->secondaryViewContainer()->caption());
        }
    } else {
        name = tabPage->activeViewContainer()->caption();
    }
    // clang-format on

    // Make sure that a '&' inside the directory name is displayed correctly
    // and not misinterpreted as a keyboard shortcut in QTabBar::setTabText()
    return KStringHandler::rsqueeze(name.replace('&', QLatin1String("&&")), 40 /* default maximum visible folder name visible */);
}

DolphinViewContainer *DolphinTabWidget::viewContainerAt(DolphinTabWidget::ViewIndex viewIndex) const
{
    const auto tabPage = tabPageAt(viewIndex.tabIndex);
    if (!tabPage) {
        return nullptr;
    }
    return viewIndex.isInPrimaryView ? tabPage->primaryViewContainer() : tabPage->secondaryViewContainer();
}

DolphinViewContainer *DolphinTabWidget::activateViewContainerAt(DolphinTabWidget::ViewIndex viewIndex)
{
    activateTab(viewIndex.tabIndex);
    auto viewContainer = viewContainerAt(viewIndex);
    if (!viewContainer) {
        return nullptr;
    }
    viewContainer->setActive(true);
    Q_EMIT activeViewChanged(viewContainer);
    return viewContainer;
}

const std::optional<const DolphinTabWidget::ViewIndex> DolphinTabWidget::viewOpenAtDirectory(const QUrl &directory) const
{
    int i = currentIndex();
    if (i < 0) {
        return std::nullopt;
    }
    // loop over the tabs starting from the current one
    do {
        const auto tabPage = tabPageAt(i);
        if (tabPage->primaryViewContainer()->url() == directory) {
            return std::optional(ViewIndex{i, true});
        }

        if (tabPage->splitViewEnabled() && tabPage->secondaryViewContainer()->url() == directory) {
            return std::optional(ViewIndex{i, false});
        }

        i = (i + 1) % count();
    } while (i != currentIndex());

    return std::nullopt;
}

const std::optional<const DolphinTabWidget::ViewIndex> DolphinTabWidget::viewShowingItem(const QUrl &item) const
{
    // The item might not be loaded yet even though it exists. So instead
    // we check if the folder containing the item is showing its contents.
    const QUrl dirContainingItem(item.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash));

    // The dirContainingItem is either open directly or expanded in a tree-style view mode.
    // Is dirContainingitem the base url of a view?
    auto viewOpenAtContainingDirectory = viewOpenAtDirectory(dirContainingItem);
    if (viewOpenAtContainingDirectory.has_value()) {
        return viewOpenAtContainingDirectory;
    }

    // Is dirContainingItem expanded in some tree-style view?
    // The rest of this method is about figuring this out.

    int i = currentIndex();
    if (i < 0) {
        return std::nullopt;
    }
    // loop over the tabs starting from the current one
    do {
        const auto tabPage = tabPageAt(i);
        if (tabPage->primaryViewContainer()->url().isParentOf(item)) {
            const KFileItem fileItemContainingItem = tabPage->primaryViewContainer()->view()->items().findByUrl(dirContainingItem);
            if (!fileItemContainingItem.isNull() && tabPage->primaryViewContainer()->view()->isExpanded(fileItemContainingItem)) {
                return std::optional(ViewIndex{i, true});
            }
        }

        if (tabPage->splitViewEnabled() && tabPage->secondaryViewContainer()->url().isParentOf(item)) {
            const KFileItem fileItemContainingItem = tabPage->secondaryViewContainer()->view()->items().findByUrl(dirContainingItem);
            if (!fileItemContainingItem.isNull() && tabPage->secondaryViewContainer()->view()->isExpanded(fileItemContainingItem)) {
                return std::optional(ViewIndex{i, false});
            }
        }

        i = (i + 1) % count();
    } while (i != currentIndex());

    return std::nullopt;
}

#include "moc_dolphintabwidget.cpp"
