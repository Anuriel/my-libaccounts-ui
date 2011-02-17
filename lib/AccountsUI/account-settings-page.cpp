/*
 * This file is part of accounts-ui
 *
 * Copyright (C) 2009-2010 Nokia Corporation.
 *
 * Contact: Alberto Mardegan <alberto.mardegan@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

//project
#include "account-settings-page-priv.h"
#include "provider-plugin-process.h"
#include "service-settings-widget.h"
#include "credentialdialog.h"
#include "service-model.h"
#include "sort-service-model.h"

//Accounts
#include <Accounts/Account>
#include <Accounts/Provider>

//Meegotouch
#include <MLayout>
#include <MLinearLayoutPolicy>
#include <MMessageBox>
#include <MAction>
#include <MButton>
#include <MImageWidget>
#include <MSeparator>

//Qt
#include <QDebug>

#define INFO_BANNER_TIMEOUT 3000

using namespace AccountsUI;

AccountSettingsPagePrivate::AccountSettingsPagePrivate(
    AbstractAccountSetupContext *context):
    context(context),
    account(0),
    usernameAndStatus(0),
    serviceSettingLayout(0),
    layoutServicePolicy(0),
    enableButton(0),
    syncHandler(0),
    changePasswordDialogStarted(false),
    panel(0),
    layout(0),
    layoutPolicy(0),
    panelPolicy(0),
    settingsExist(false)
{
    account = context->account();
    serviceList = account->services();
    abstractContexts.append(context);
    panel = new MWidgetController();
    syncHandler = new AccountSyncHandler(this);
    connect(syncHandler, SIGNAL(syncStateChanged(const SyncState&)),
            this, SLOT(onSyncStateChanged(const SyncState&)));
}

void AccountSettingsPagePrivate::saveSettings()
{
    Q_Q(AccountSettingsPage);
    disconnect(this , SIGNAL(backButtonClicked()), 0, 0);
    q->setProgressIndicatorVisible(true);
    qDebug() << Q_FUNC_INFO;
    if (enableButton) {
        bool state = enableButton->isChecked();
        if (serviceList.count() == 1) {
            account->selectService(serviceList.at(0));
            if (account->enabled() != state)
                account->setEnabled(state);
        } else if (serviceList.count() > 1) {
            foreach (AbstractServiceSetupContext *serviceContext, contexts) {
                const Accounts::Service *service = serviceContext->service();
                QMap<QString, bool>::iterator i =
                        serviceStatusMap.find(service->name());
                if (i == serviceStatusMap.end())
                    continue;
                account->selectService(service);
                if (account->enabled() != i.value())
                        serviceContext->enable(i.value());
                serviceStatusMap.remove(i.key());
            }
        }

        context->account()->selectService(NULL);
        if (account->enabled() != state)
            account->setEnabled(state);
    }
    //we should call only validate. Storing will be handled
    //in onSyncStateChangted func.
    syncHandler->validate(abstractContexts);
}

void AccountSettingsPagePrivate::onSyncStateChanged(const SyncState &state)
{
    qDebug() << Q_FUNC_INFO;

    Q_Q(AccountSettingsPage);
    switch (state) {
        case NotValidated:
            qDebug() << Q_FUNC_INFO << "NotValidated";
            q->setProgressIndicatorVisible(false);
            //Saving the settings on back button press
            connect(this, SIGNAL(backButtonClicked()),
                    this, SLOT(saveSettings()));
            break;
        case Validated:
            qDebug() << Q_FUNC_INFO << "Validated";
            syncHandler->store(abstractContexts);
            break;
        case NotStored:
            qDebug() << Q_FUNC_INFO << "NotStored";
            connect(context->account(), SIGNAL(synced()),
                    ProviderPluginProcess::instance(), SLOT(quit()));
            context->account()->sync();
            break;
        case Stored:
            qDebug() << Q_FUNC_INFO << "Stored";
            connect(context->account(), SIGNAL(synced()),
                    ProviderPluginProcess::instance(), SLOT(quit()));
            context->account()->sync();
            break;
        default:
            return;
    }
}

void AccountSettingsPagePrivate::openChangePasswordDialog()
{
    //ignore multiple clicks
    if (changePasswordDialogStarted)
    {
        qDebug() << Q_FUNC_INFO << "Change password dialog is started already";
        return;
    }

    changePasswordDialogStarted = true;

    CredentialDialog *credentialDialog = new CredentialDialog(account->credentialsId());
    if (!credentialDialog) {
        qCritical() << "Cannot create change password dialog";
        return;
    }
    credentialDialog->setParent(this);
    connect (credentialDialog, SIGNAL(safeToDeleteMe(CredentialDialog*)),
             this, SLOT(deleteCredentialsDialog()));
    //% "Change Password"
    credentialDialog->setTitle(qtTrId("qtn_acc_login_title_change"));
    credentialDialog->exec();
}

void AccountSettingsPagePrivate::deleteCredentialsDialog()
{
    changePasswordDialogStarted = false;
    CredentialDialog *credentialDialog;

    if (sender() != NULL &&
        (credentialDialog = qobject_cast<CredentialDialog *>(sender())) != NULL)
        credentialDialog->deleteLater();
}


/*
 * The same serviceTypes cannot be enabled in meantime
 * */
void AccountSettingsPagePrivate::disableSameServiceTypes(const QString &serviceType)
{
    qDebug() << Q_FUNC_INFO << __LINE__;
    if (!sender())
    {
        qCritical() << "disableSameServiceTypes() must be called via signaling";
        return;
    }

    if (settingsWidgets.count(serviceType) == 1)
        return;

    foreach (ServiceSettingsWidget *widget, settingsWidgets.values(serviceType)) {
        if (widget == sender())
            continue;

        widget->setServiceButtonEnable(false);
    }
}

void AccountSettingsPagePrivate::setEnabledService(const QString &serviceName,
                                                   bool enabled)
{
    serviceStatusMap[serviceName] = enabled;
}

AccountSettingsPage::AccountSettingsPage(AbstractAccountSetupContext *context)
        : MApplicationPage(),
          d_ptr(new AccountSettingsPagePrivate(context))
{
    Q_D(AccountSettingsPage);

    Q_ASSERT (context != NULL);
    d->q_ptr = this;

    //Saving the settings on back button press
    connect(this, SIGNAL(backButtonClicked()),
            d, SLOT(saveSettings()));
    setStyleName("AccountsUiPage");
}

AccountSettingsPage::~AccountSettingsPage()
{
    delete d_ptr;
}

void AccountSettingsPage::setServicesToBeShown()
{
    Q_D(AccountSettingsPage);
    /* List the services available on the account and load all the respective plugins. */

    //% "%1 Settings"
    setTitle(qtTrId("qtn_acc_ser_prof_set_title").arg(d->context->account()->providerName()));

    ServiceModel *serviceModel = new ServiceModel(d->context->account(), this);
    SortServiceModel *sortModel = new SortServiceModel(this);
    sortModel->setSourceModel(serviceModel);
    sortModel->setEnabledServices(d->context->account()->enabledServices());
    sortModel->setHiddenServices(d->hiddenServiceList);
    sortModel->sort(ServiceModel::ServiceNameColumn);

    d->contexts = ServiceModel::createServiceContexts(sortModel, d->context, this);

    /* iterate through the contexts we created for each service, and get the
     * UI widgets to embed */
    QMap<QString, bool> enabledServiceTypes;
    MLayout *layoutPanel = new MLayout(d->panel);
    d->panelPolicy = new MLinearLayoutPolicy(layoutPanel, Qt::Vertical);

    foreach (AbstractServiceSetupContext *context, d->contexts) {
        d->abstractContexts.append(context);
        const Accounts::Service *service = context->service();
        ServiceSettingsWidget *settingsWidget;

        d->account->selectService(service);
        d->serviceStatusMap.insert(service->name(), d->account->enabled());
        emit serviceEnabled(service->name(), d->account->enabled());
        bool enabled = false;
        if (d->account->enabled() &&
            !enabledServiceTypes.contains(service->serviceType())) {
            enabledServiceTypes.insert(service->serviceType(), true);
            enabled = true;
        }

        if (d->serviceList.count() > 1)
            settingsWidget = new ServiceSettingsWidget(context,
                                                   d->panel,
                                                   ServiceSettingsWidget::EnableButton,
                                                   enabled);
        else
            settingsWidget = new ServiceSettingsWidget(context,
                                                       d->panel,
                                                       ServiceSettingsWidget::MandatorySettings |
                                                       ServiceSettingsWidget::NonMandatorySettings,
                                                       enabled);

        d->settingsWidgets.insertMulti(service->serviceType(), settingsWidget);
        d->panelPolicy->addItem(settingsWidget);
    }

    d->layoutServicePolicy->addItem(d->panel);
    /*
     * no need in extra processing of any signals during content creation
     * */

    if (d->settingsWidgets.count() > 1)
        d->settingsExist = true;

    foreach (ServiceSettingsWidget *settingsWidget, d->settingsWidgets) {
        connect (settingsWidget, SIGNAL(serviceButtonEnabled(const QString&)),
                 this, SLOT(disableSameServiceTypes(const QString&)));
        connect (settingsWidget, SIGNAL(serviceEnabled(const QString&, bool)),
                 this, SIGNAL(serviceEnabled(const QString&, bool)));
        connect (settingsWidget, SIGNAL(serviceEnabled(const QString&, bool)),
                 this, SLOT(setEnabledService(const QString&, bool)));
    }
}

QGraphicsLayoutItem *AccountSettingsPage::createAccountSettingsLayout()
{
    Q_D(AccountSettingsPage);

    // First, see if the plugin has own implementation of this widget
    QGraphicsLayoutItem *accountSettingsWidget = d->context->widget();
    if (accountSettingsWidget != 0)
        return accountSettingsWidget;

    // Generic implementation
    MWidget *upperWidget = new MWidget(this);
    MLayout *upperLayout = new MLayout(upperWidget);
    MLinearLayoutPolicy *upperLayoutPolicy =
        new MLinearLayoutPolicy(upperLayout, Qt::Vertical);
    upperLayoutPolicy->setSpacing(0);

    MLayout *horizontalLayout = new MLayout();
    MLinearLayoutPolicy *horizontalLayoutPolicy =
        new MLinearLayoutPolicy(horizontalLayout, Qt::Horizontal);
    horizontalLayoutPolicy->setSpacing(0);

    QString providerName(d->account->providerName());
    QString providerIconId;
    Accounts::Provider *provider =
        AccountsManager::instance()->provider(providerName);
    if (provider) {
        providerIconId = provider->iconName();
    }

    d->usernameAndStatus =
        new MDetailedListItem(MDetailedListItem::IconTitleSubtitleAndTwoSideIcons);
    d->usernameAndStatus->setStyleName("CommonDetailedListItemInverted");
    d->usernameAndStatus->setObjectName("wgAccountSettingsPageListItem");
    d->usernameAndStatus->imageWidget()->setImage(providerIconId);
    d->usernameAndStatus->setTitle(providerName);
    d->usernameAndStatus->setSubtitle(d->account->displayName());

    MSeparator *separatorTop = new MSeparator(this);
    separatorTop->setOrientation(Qt::Horizontal);
    separatorTop->setStyleName("CommonItemDividerInverted");

    d->enableButton = new MButton(this);
    d->enableButton->setViewType(MButton::switchType);
    d->enableButton->setStyleName("CommonSwitchInverted");
    d->enableButton->setCheckable(true);

    d->account->selectService(NULL);
    if (d->account->enabled()) {
        d->panel->setEnabled(true);
        d->enableButton->setChecked(true);
    } else {
        d->panel->setEnabled(false);
        d->enableButton->setChecked(false);
    }

    connect(d->enableButton, SIGNAL(toggled(bool)), this, SLOT(enable(bool)));

    horizontalLayoutPolicy->addItem(d->usernameAndStatus,
                                    Qt::AlignLeft | Qt::AlignVCenter);
    horizontalLayoutPolicy->addItem(d->enableButton,
                                    Qt::AlignRight | Qt::AlignVCenter);
    upperLayoutPolicy->addItem(horizontalLayout);
    upperLayoutPolicy->addItem(separatorTop);

    return upperWidget;
}

void AccountSettingsPage::createContent()
{
    Q_D(AccountSettingsPage);

    if (d->context == 0) return;

    //we need a central widget to get the right layout size under the menubar
    MWidget *centralWidget = new MWidget();
    d->layout = new MLayout(centralWidget);
    d->layoutPolicy = new MLinearLayoutPolicy(d->layout, Qt::Vertical);
    d->layoutPolicy->setSpacing(0);

    QGraphicsLayoutItem *accountSettingsLayout = createAccountSettingsLayout();
    d->layoutPolicy->addItem(accountSettingsLayout);

    MWidget *serviceWidget = new MWidget(this);
    d->serviceSettingLayout = new MLayout(serviceWidget);
    d->layoutServicePolicy = new MLinearLayoutPolicy(d->serviceSettingLayout, Qt::Vertical);
    d->layoutServicePolicy->setSpacing(0);

    /* Sets the service widgets and add it into the layout policy*/
    setServicesToBeShown();

    setCentralWidget(centralWidget);

    //% "Delete"
    MAction *action = new MAction(qtTrId("qtn_comm_command_delete"),this);
    action->setLocation(MAction::ApplicationMenuLocation);
    addAction(action);
    connect(action, SIGNAL(triggered()),
            this, SLOT(removeAccount()));

    d->layoutPolicy->addItem(serviceWidget);
    if (d->settingsExist) {
        MSeparator *separatorBottom = new MSeparator(this);
        separatorBottom->setStyleName("CommonItemDividerInverted");
        separatorBottom->setOrientation(Qt::Horizontal);
        d->layoutServicePolicy->addItem(separatorBottom);
    }
    d->layoutPolicy->addStretch();
}

const AbstractAccountSetupContext *AccountSettingsPage::context()
{
    Q_D(AccountSettingsPage);
    return d->context;
}

void AccountSettingsPage::enable(bool state)
{
    Q_D(AccountSettingsPage);
    d->panel->setEnabled(state);
}

void AccountSettingsPage::removeAccount()
{
    Q_D(AccountSettingsPage);
    //% "Delete %1 from your device?"
    QString dialogTitle =
        qtTrId("qtn_acc_remove_account").arg(d->context->account()->displayName());
    //% "All content related to this account will be deleted permanently"
    MMessageBox removeMBox(dialogTitle, qtTrId("qtn_acc_remove_account_statement"),
                           M::YesButton | M::NoButton);
    removeMBox.setStyleName("RemoveDialog");

    if (removeMBox.exec() == M::YesButton) {
        d->context->account()->remove();
        d->context->account()->sync();
        ProviderPluginProcess::instance()->quit();
    }
}

void AccountSettingsPage::setWidget(MWidget *widget)
{
     Q_D(AccountSettingsPage);
     d->panelPolicy->addItem(widget);
}

void AccountSettingsPage::setHiddenServices(const Accounts::ServiceList &hiddenServices)
{
    Q_D(AccountSettingsPage);
    d->hiddenServiceList = hiddenServices;
}

MButton *AccountSettingsPage::enableAccountButton() const
{
    Q_D(const AccountSettingsPage);
    return d->enableButton;
}

