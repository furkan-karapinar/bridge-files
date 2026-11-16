/*
    This file is part of the KDE project
    SPDX-FileCopyrightText: 2020 Felix Ernst <felixernst@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "dolphinurlnavigator.h"

#include "dolphin_generalsettings.h"
#include "dolphinplacesmodelsingleton.h"
#include "dolphinurlnavigatorscontroller.h"
#include "global.h"
#include "shellkit.h"

#include <KFilePlacesModel>
#include <KIO/Global>
#include <KLocalizedString>
#include <KUrlComboBox>

#include <QAbstractButton>
#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QStandardPaths>

#include <algorithm>

namespace
{
/**
 * shellkit fork: the address bar's menus as in Windows. The chevron at the left lists the
 * top-level places (Home, Gallery, the user folders, This PC, Network, Recycle Bin) after the
 * folders that do not fit, and the folder menus of the other chevrons have folder icons.
 * KUrlNavigator builds these menus privately: they are completed when they are polished, just
 * before they are laid out. The path menu opens the URL in an action's data, so added entries
 * work as its own do.
 */
class NavigatorMenus : public QObject
{
public:
    using QObject::QObject;

    static void install()
    {
        static NavigatorMenus *menus = nullptr;
        if (!menus) {
            menus = new NavigatorMenus(qApp);
            qApp->installEventFilter(menus);
        }
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Polish) {
            if (auto menu = qobject_cast<QMenu *>(watched)) {
                if (auto navigator = qobject_cast<KUrlNavigator *>(menu->parent())) {
                    if (isPathMenu(menu)) {
                        completePathMenu(menu, navigator);
                    }
                } else if (menu->inherits("KDEPrivate::KUrlNavigatorMenu")) {
                    for (QAction *action : menu->actions()) {
                        if (!action->isSeparator() && !action->menu() && action->icon().isNull()) {
                            action->setIcon(QIcon::fromTheme(QStringLiteral("folder")));
                        }
                    }
                }
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    static bool isPathMenu(const QMenu *menu)
    {
        // The path menu carries KUrlNavigatorPathSelectorEventFilter; the context menu does not.
        const QList<QObject *> children = menu->children();
        return std::any_of(children.cbegin(), children.cend(), [](const QObject *child) {
            return child->inherits("KDEPrivate::KUrlNavigatorPathSelectorEventFilter");
        });
    }

    static void completePathMenu(QMenu *menu, KUrlNavigator *navigator)
    {
        // Keep only the folders whose buttons do not fit, inside the place; the place itself
        // and its parents (like Home's) are not hidden. KIO lists one entry per button from the
        // place on, and the visible buttons are always the last ones.
        const KFilePlacesModel *places = DolphinPlacesModelSingleton::instance().placesModel();
        const QModelIndex placeIndex = places->closestItem(navigator->locationUrl());
        const QUrl place = placeIndex.isValid() ? places->url(placeIndex) : QUrl();
        const QList<QWidget *> buttons = navigator->findChildren<QWidget *>();
        const auto visibleButtons = std::count_if(buttons.cbegin(), buttons.cend(), [navigator](const QWidget *button) {
            return button->inherits("KDEPrivate::KUrlNavigatorButton") && button->isVisibleTo(navigator);
        });
        const QList<QAction *> actions = menu->actions();
        const auto entries = std::count_if(actions.cbegin(), actions.cend(), [](const QAction *action) {
            return !action->isSeparator();
        });
        auto hiddenEntries = entries - visibleButtons;
        int kept = 0;
        for (QAction *action : actions) {
            const bool hidden = !action->isSeparator() && hiddenEntries-- > 0;
            const QUrl url(action->data().toString());
            if (hidden && (place.isEmpty() || place.isParentOf(url))) {
                action->setText(action->text().trimmed());
                action->setIcon(QIcon::fromTheme(QStringLiteral("folder")));
                ++kept;
            } else {
                menu->removeAction(action);
                action->deleteLater();
            }
        }
        if (kept > 0) {
            menu->addSeparator();
        }

        const auto add = [menu](const QIcon &icon, const QString &text, const QUrl &url) {
            QAction *action = menu->addAction(icon, text);
            action->setData(url.toString());
        };
        add(QIcon::fromTheme(QStringLiteral("go-home")), i18ndc(shellkitDomain, "@item:inlistbox Start page of the file explorer", "Home"), shellkitHomeUrl());
        const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        add(QIcon::fromTheme(QStringLiteral("folder-pictures")), i18ndc(shellkitDomain, "@item:inlistbox Photo collection", "Gallery"), QUrl::fromLocalFile(pictures));
        menu->addSeparator();
        const QUrl home = QUrl::fromLocalFile(QDir::homePath());
        add(QIcon::fromTheme(QStringLiteral("user-home")), QFileInfo(QDir::homePath()).fileName(), home);
        const QStandardPaths::StandardLocation userFolders[] = {
            QStandardPaths::DesktopLocation,
            QStandardPaths::DownloadLocation,
            QStandardPaths::DocumentsLocation,
            QStandardPaths::PicturesLocation,
            QStandardPaths::MusicLocation,
            QStandardPaths::MoviesLocation,
        };
        for (const auto location : userFolders) {
            const QString path = QStandardPaths::writableLocation(location);
            if (!path.isEmpty() && path != QDir::homePath() && QDir(path).exists()) {
                const QUrl url = QUrl::fromLocalFile(path);
                add(QIcon::fromTheme(KIO::iconNameForUrl(url)), QDir(path).dirName(), url);
            }
        }
        menu->addSeparator();
        add(QIcon::fromTheme(QStringLiteral("computer")), i18ndc(shellkitDomain, "@item:inlistbox", "This PC"), shellkitThisPcUrl());
        add(QIcon::fromTheme(QStringLiteral("network-workgroup")), i18ndc(shellkitDomain, "@item:inlistbox", "Network"), shellkitNetworkUrl());
        add(QIcon::fromTheme(QStringLiteral("user-trash")), i18ndc(shellkitDomain, "@item:inmenu", "Recycle Bin"), QUrl(QStringLiteral("trash:/")));
    }
};
}

DolphinUrlNavigator::DolphinUrlNavigator(QWidget *parent)
    : DolphinUrlNavigator(QUrl(), parent)
{
}

DolphinUrlNavigator::DolphinUrlNavigator(const QUrl &url, QWidget *parent)
    : KUrlNavigator(DolphinPlacesModelSingleton::instance().placesModel(), url, parent)
{
    const GeneralSettings *settings = GeneralSettings::self();
    setUrlEditable(settings->editableUrl());
    setShowFullPath(settings->showFullPath());
    setHomeUrl(Dolphin::homeUrl());
    NavigatorMenus::install();
    setPlacesSelectorVisible(DolphinUrlNavigatorsController::placesSelectorVisible());
    editor()->setCompletionMode(KCompletion::CompletionMode(settings->urlCompletionMode()));
    setWhatsThis(xi18nc("@info:whatsthis location bar",
                        "<para>This describes the location of the files and folders "
                        "displayed below.</para><para>The name of the currently viewed "
                        "folder can be read at the very right. To the left of it is the "
                        "name of the folder that contains it. The whole line is called "
                        "the <emphasis>path</emphasis> to the current location because "
                        "following these folders from left to right leads here.</para>"
                        "<para>This interactive path "
                        "is more powerful than one would expect. To learn more "
                        "about the basic and advanced features of the location bar "
                        "<link url='help:/dolphin/location-bar.html'>click here</link>. "
                        "This will open the dedicated page in the Handbook.</para>"));

    DolphinUrlNavigatorsController::registerDolphinUrlNavigator(this);

    connect(this, &KUrlNavigator::returnPressed, this, &DolphinUrlNavigator::slotReturnPressed);

    auto readOnlyBadge = new QLabel();
    readOnlyBadge->setPixmap(QIcon::fromTheme(QStringLiteral("emblem-readonly")).pixmap(12, 12));
    readOnlyBadge->setToolTip(i18nc("@info:tooltip of a 'locked' symbol in url navigator", "This folder is not writable for you."));
    readOnlyBadge->hide();
    setBadgeWidget(readOnlyBadge);
}

DolphinUrlNavigator::~DolphinUrlNavigator()
{
    DolphinUrlNavigatorsController::unregisterDolphinUrlNavigator(this);
}

QSize DolphinUrlNavigator::sizeHint() const
{
    if (isUrlEditable()) {
        return editor()->lineEdit()->sizeHint();
    }
    int widthHint = 0;
    for (int i = 0; i < layout()->count(); ++i) {
        QWidget *widget = layout()->itemAt(i)->widget();
        const QAbstractButton *button = qobject_cast<QAbstractButton *>(widget);
        if (button && button->icon().isNull()) {
            widthHint += widget->minimumSizeHint().width();
        }
    }
    if (readOnlyBadgeVisible()) {
        widthHint += badgeWidget()->sizeHint().width();
    }
    return QSize(widthHint, KUrlNavigator::sizeHint().height());
}

std::unique_ptr<DolphinUrlNavigator::VisualState> DolphinUrlNavigator::visualState() const
{
    std::unique_ptr<VisualState> visualState{new VisualState};
    visualState->isUrlEditable = (isUrlEditable());
    const QLineEdit *lineEdit = editor()->lineEdit();
    visualState->hasFocus = lineEdit->hasFocus();
    visualState->text = lineEdit->text();
    visualState->cursorPosition = lineEdit->cursorPosition();
    visualState->selectionStart = lineEdit->selectionStart();
    visualState->selectionLength = lineEdit->selectionLength();
    return visualState;
}

void DolphinUrlNavigator::setVisualState(const VisualState &visualState)
{
    setUrlEditable(visualState.isUrlEditable);
    if (!visualState.isUrlEditable) {
        return;
    }
    editor()->lineEdit()->setText(visualState.text);
    if (visualState.hasFocus) {
        editor()->lineEdit()->setFocus();
        editor()->lineEdit()->setCursorPosition(visualState.cursorPosition);
        if (visualState.selectionStart != -1) {
            editor()->lineEdit()->setSelection(visualState.selectionStart, visualState.selectionLength);
        }
    }
}

void DolphinUrlNavigator::clearText() const
{
    editor()->lineEdit()->clear();
}

void DolphinUrlNavigator::setPlaceholderText(const QString &text)
{
    editor()->lineEdit()->setPlaceholderText(text);
}

void DolphinUrlNavigator::setReadOnlyBadgeVisible(bool visible)
{
    QWidget *readOnlyBadge = badgeWidget();
    if (readOnlyBadge) {
        readOnlyBadge->setVisible(visible);
    }
}

bool DolphinUrlNavigator::readOnlyBadgeVisible() const
{
    QWidget *readOnlyBadge = badgeWidget();
    if (readOnlyBadge) {
        return readOnlyBadge->isVisible();
    }
    return false;
}

void DolphinUrlNavigator::slotReturnPressed()
{
    if (!GeneralSettings::editableUrl()) {
        setUrlEditable(false);
    }
}

void DolphinUrlNavigator::keyPressEvent(QKeyEvent *keyEvent)
{
    if (keyEvent->key() == Qt::Key_Escape && !isUrlEditable()) {
        Q_EMIT requestToLoseFocus();
        return;
    }
    KUrlNavigator::keyPressEvent(keyEvent);
}

#include "moc_dolphinurlnavigator.cpp"
