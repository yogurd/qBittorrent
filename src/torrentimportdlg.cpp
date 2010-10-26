/*
 * Bittorrent Client using Qt4 and libtorrent.
 * Copyright (C) 2010  Christophe Dumez
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link this program with the OpenSSL project's "OpenSSL" library (or with
 * modified versions of it that use the same license as the "OpenSSL" library),
 * and distribute the linked executables. You must obey the GNU General Public
 * License in all respects for all of the code used other than "OpenSSL".  If you
 * modify file(s), you may extend this exception to your version of the file(s),
 * but you are not obligated to do so. If you do not wish to do so, delete this
 * exception statement from your version.
 *
 * Contact : chris@qbittorrent.org
 */

#include <QFileDialog>
#include <QMessageBox>

#include "torrentimportdlg.h"
#include "ui_torrentimportdlg.h"
#include "qinisettings.h"
#include "qbtsession.h"
#include "torrentpersistentdata.h"
#include "misc.h"

using namespace libtorrent;

TorrentImportDlg::TorrentImportDlg(QWidget *parent) :
  QDialog(parent),
  ui(new Ui::TorrentImportDlg)
{
  ui->setupUi(this);
  // Libtorrent < 0.15 does not support skipping file checking
#if LIBTORRENT_VERSION_MINOR < 15
  ui->checkSkipCheck->setVisible(false);
#endif
}

TorrentImportDlg::~TorrentImportDlg()
{
  delete ui;
}

void TorrentImportDlg::on_browseTorrentBtn_clicked()
{
  QIniSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent"));
  const QString default_dir = settings.value(QString::fromUtf8("MainWindowLastDir"), QDir::homePath()).toString();
  // Ask for a torrent file
  m_torrentPath = QFileDialog::getOpenFileName(this, tr("Torrent file to import"), default_dir, tr("Torrent files (*.torrent)"));
  if(!m_torrentPath.isEmpty()) {
    loadTorrent(m_torrentPath);
  } else {
    ui->lineTorrent->clear();
  }
}

void TorrentImportDlg::on_browseContentBtn_clicked()
{
  QIniSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent"));
  const QString default_dir = settings.value(QString::fromUtf8("TorrentImport/LastContentDir"), QDir::homePath()).toString();
  if(t->num_files() == 1) {
    // Single file torrent
    const QString file_name = misc::toQStringU(t->file_at(0).path.leaf());
    qDebug("Torrent has only one file: %s", qPrintable(file_name));
    QString extension = misc::file_extension(file_name);
    qDebug("File extension is : %s", qPrintable(extension));
    QString filter;
    if(!extension.isEmpty()) {
      extension = extension.toUpper();
      filter = tr("%1 Files", "%1 is a file extension (e.g. PDF)").arg(extension)+" (*."+extension+")";
    }
    m_contentPath = QFileDialog::getOpenFileName(this, tr("Please provide the location of %1", "%1 is a file name").arg(file_name), default_dir, filter);
    if(m_contentPath.isEmpty() || !QFile(m_contentPath).exists()) {
      m_contentPath = QString::null;
      ui->importBtn->setEnabled(false);
      ui->checkSkipCheck->setEnabled(false);
      return;
    }
    // Update display
#if defined(Q_WS_WIN) || defined(Q_OS_OS2)
    ui->lineContent->setText(m_contentPath.replace("/", "\\");
    #else
    ui->lineContent->setText(m_contentPath);
#endif
#if LIBTORRENT_VERSION_MINOR >= 15
    // Check file size
    const qint64 file_size = QFile(m_contentPath).size();
    if(t->file_at(0).size == file_size) {
      qDebug("The file size matches, allowing fast seeding...");
      ui->checkSkipCheck->setEnabled(true);
    } else {
      qDebug("The file size does not match, forbidding fast seeding...");
      ui->checkSkipCheck->setChecked(false);
      ui->checkSkipCheck->setEnabled(false);
    }
#endif
    // Handle file renaming
    QStringList parts = m_contentPath.replace("\\", "/").split("/");
    QString new_file_name = parts.takeLast();
    if(new_file_name != file_name) {
      qDebug("The file has been renamed");
      QStringList new_parts = m_filesPath.first().split("/");
      new_parts.replace(new_parts.count()-1, new_file_name);
      m_filesPath.replace(0, new_parts.join("/"));
    }
    m_contentPath = parts.join("/");
  } else {
    // multiple files torrent
    m_contentPath = QFileDialog::getExistingDirectory(this, tr("Please point to the location of the torrent: %1").arg(misc::toQStringU(t->name())),
                                                      default_dir);
    if(m_contentPath.isEmpty() || !QDir(m_contentPath).exists()) {
      m_contentPath = QString::null;
      ui->importBtn->setEnabled(false);
      ui->checkSkipCheck->setEnabled(false);
      return;
    }
    // Update the display
#if defined(Q_WS_WIN) || defined(Q_OS_OS2)
    ui->lineContent->setText(m_contentPath.replace("/", "\\");
    #else
    ui->lineContent->setText(m_contentPath);
#endif
#if LIBTORRENT_VERSION_MINOR >= 15
    bool size_mismatch = false;
    QDir content_dir(m_contentPath);
    // Check file sizes
    torrent_info::file_iterator it; t->begin_files();
    for(it = t->begin_files(); it != t->end_files(); it++) {
      if(QFile(QDir::cleanPath(content_dir.absoluteFilePath(misc::toQStringU(it->path.string())))).size() != it->size) {
        qDebug("%s is %lld",
               qPrintable(QDir::cleanPath(content_dir.absoluteFilePath(misc::toQStringU(it->path.string())))), (long long int) QFile(QDir::cleanPath(content_dir.absoluteFilePath(misc::toQStringU(it->path.string())))).size());
        qDebug("%s is %lld",
               it->path.string().c_str(), (long long int)it->size);
        size_mismatch = true;
        break;
      }
    }
    if(size_mismatch) {
      qDebug("The file size does not match, forbidding fast seeding...");
      ui->checkSkipCheck->setChecked(false);
      ui->checkSkipCheck->setEnabled(false);
    } else {
      qDebug("The file size matches, allowing fast seeding...");
      ui->checkSkipCheck->setEnabled(true);
    }
#endif
  }
  // Enable the import button
  ui->importBtn->setEnabled(true);
}

void TorrentImportDlg::on_importBtn_clicked()
{
  accept();
}

QString TorrentImportDlg::getTorrentPath() const
{
  return m_torrentPath;
}

QString TorrentImportDlg::getContentPath() const
{
  return m_contentPath;
}

void TorrentImportDlg::importTorrent(QBtSession *BTSession)
{
  TorrentImportDlg dlg;
  if(dlg.exec()) {
    boost::intrusive_ptr<libtorrent::torrent_info> t = dlg.torrent();
    if(!t->is_valid())
      return;
    QString torrent_path = dlg.getTorrentPath();
    QString content_path = dlg.getContentPath();
    if(torrent_path.isEmpty() || content_path.isEmpty() || !QFile(torrent_path).exists()) return;
    const QString hash = misc::toQString(t->info_hash());
    TorrentTempData::setSavePath(hash, content_path);
#if LIBTORRENT_VERSION_MINOR >= 15
    TorrentTempData::setSeedingMode(hash, dlg.skipFileChecking());
#endif
    qDebug("Adding the torrent to the session...");
    BTSession->addTorrent(torrent_path);
    // Remember the last opened folder
    QIniSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent"));
    settings.setValue(QString::fromUtf8("MainWindowLastDir"), torrent_path);
    settings.setValue("TorrentImport/LastContentDir", content_path);
    return;
  }
  return;
}

void TorrentImportDlg::loadTorrent(const QString &torrent_path)
{
  // Load the torrent file
  try {
    t = new torrent_info(torrent_path.toUtf8().constData());
    if(!t->is_valid() || t->num_files() == 0)
      throw std::exception();
  } catch(std::exception&) {
    ui->browseContentBtn->setEnabled(false);
    ui->lineTorrent->clear();
    QMessageBox::warning(this, tr("Invalid torrent file"), tr("This is not a valid torrent file."));
    return;
  }
  // The torrent file is valid
  misc::truncateRootFolder(t);
  // Update display
#if defined(Q_WS_WIN) || defined(Q_OS_OS2)
  ui->lineTorrent->setText(torrent_path.replace("/", "\\"));
#else
  ui->lineTorrent->setText(torrent_path);
#endif
  ui->browseContentBtn->setEnabled(true);
  // Load the file names
  initializeFilesPath();
}

void TorrentImportDlg::initializeFilesPath()
{
  m_filesPath.clear();
  // Loads files path in the torrent
  for(int i=0; i<t->num_files(); ++i) {
    m_filesPath << misc::toQStringU(t->file_at(i).path.string()).replace("\\", "/");
  }
}

bool TorrentImportDlg::fileRenamed() const
{
  return m_fileRenamed;
}


boost::intrusive_ptr<libtorrent::torrent_info> TorrentImportDlg::torrent() const
{
  return t;
}

#if LIBTORRENT_VERSION_MINOR >= 15
bool TorrentImportDlg::skipFileChecking() const
{
  return ui->checkSkipCheck->isChecked();
}
#endif