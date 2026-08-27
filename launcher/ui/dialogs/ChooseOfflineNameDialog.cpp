// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2025 Octol1ttle <l1ttleofficial@outlook.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "ChooseOfflineNameDialog.h"

#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

#include "ui_ChooseOfflineNameDialog.h"

ChooseOfflineNameDialog::ChooseOfflineNameDialog(const QString& message, QWidget* parent)
    : QDialog(parent), ui(new Ui::ChooseOfflineNameDialog)
{
    ui->setupUi(this);
    
    if (!message.isEmpty()) {
        ui->label->setText(message);
    } else {
        ui->label->setText(tr("Enter your desired offline player name:"));
    }

    ui->buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    ui->buttonBox->button(QDialogButtonBox::Ok)->setText(tr("OK"));

    // السماح بأسماء ماينكرافت القياسية من 3 إلى 16 حرف/رقم
    const QRegularExpression usernameRegExp("^[A-Za-z0-9_]{1,16}$");
    m_usernameValidator = new QRegularExpressionValidator(usernameRegExp, this);
    ui->usernameTextBox->setValidator(m_usernameValidator);

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // التحقق المبدئي من صحة الإدخال
    updateAcceptAllowed(ui->usernameTextBox->text());
}

ChooseOfflineNameDialog::~ChooseOfflineNameDialog()
{
    delete ui;
}

QString ChooseOfflineNameDialog::getUsername() const
{
    return ui->usernameTextBox->text().trimmed();
}

void ChooseOfflineNameDialog::setUsername(const QString& username) const
{
    ui->usernameTextBox->setText(username.trimmed());
    updateAcceptAllowed(username.trimmed());
}

void ChooseOfflineNameDialog::updateAcceptAllowed(const QString& username) const
{
    const bool isAllowed = ui->allowInvalidUsernames->isChecked()
                               ? !username.trimmed().isEmpty()
                               : (!username.trimmed().isEmpty() && ui->usernameTextBox->hasAcceptableInput());

    auto* okButton = ui->buttonBox->button(QDialogButtonBox::Ok);
    if (okButton) {
        okButton->setEnabled(isAllowed);
    }
}

void ChooseOfflineNameDialog::on_usernameTextBox_textEdited(const QString& newText) const
{
    updateAcceptAllowed(newText);
}

void ChooseOfflineNameDialog::on_allowInvalidUsernames_checkStateChanged(const Qt::CheckState checkState) const
{
    ui->usernameTextBox->setValidator(checkState == Qt::Checked ? nullptr : m_usernameValidator);
    updateAcceptAllowed(getUsername());
}