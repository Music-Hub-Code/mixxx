/***************************************************************************
                          mixxx.cpp  -  description
                             -------------------
    begin                : Mon Feb 18 09:48:17 CET 2002
    copyright            : (C) 2002 by Tue and Ken Haste Andersen
    email                : 
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <qaccel.h>
#include <qpushbutton.h>
#include <qtable.h>
#include <qlistview.h>
#include <qiconset.h>
#include <qptrlist.h>
#include <qcombobox.h>
#include <qmessagebox.h>
#include <signal.h>

#include "wknob.h"
#include "wslider.h"
#include "wplayposslider.h"
#include "mixxx.h"
#include "filesave.xpm"
#include "fileopen.xpm"
#include "filenew.xpm"
#include "images/a.xpm"
#include "images/b.xpm"
#include "controlnull.h"

#ifdef __ALSA__
  #include "playeralsa.h"
#endif

#ifdef __ALSA__
  #include "midiobjectalsa.h"
#endif

#ifdef __PORTAUDIO__
  #include "playerportaudio.h"
#endif

#ifdef __PORTMIDI__
  #include "midiobjectportmidi.h"
#endif

#ifdef __OSSMIDI__
  #include "midiobjectoss.h"
#endif

MixxxApp::MixxxApp(QApplication *a)
{
  qDebug("Start");
  setCaption(tr("Mixxx " VERSION));

  app = a;

  ///////////////////////////////////////////////////////////////////
  // call inits to invoke all other construction parts
  initActions();
  initMenuBar();
  initToolBar();
  //initStatusBar();

  // Reset pointer to preference dialog
  pDlg = 0;

  // Initialize first available configuration
  configMap = new ConfigMapping();
  QStringList *list = configMap->getConfigurations();
  QStringList::Iterator it = list->begin();
  //for (; it != list->end(); ++it)
  qDebug("Using config %s",(*it).ascii());
  ConfigObject *config = configMap->setConfiguration((*it).ascii());

  initDoc();
  initView();

  qDebug("Init playlist");

  // Construct popup menu used to select playback channel on track selection
  playSelectMenu = new QPopupMenu(this);
  playSelectMenu->insertItem(QIconSet(a_xpm), "Player A",this, SLOT(slotChangePlay_1()));
  playSelectMenu->insertItem(QIconSet(b_xpm), "Player B",this, SLOT(slotChangePlay_2()));

  // Connect play list table selection with "select channel" popup menu
  connect(view->playlist->ListPlaylist, SIGNAL(pressed(QListViewItem *, const QPoint &, int)),
          this,                         SLOT(slotSelectPlay(QListViewItem *, const QPoint &, int)));

  // Initialize midi:
  qDebug("Init midi...");
#ifdef __ALSA__
  midi = new MidiObjectALSA(config,app);
#endif
#ifdef __PORTMIDI__
  midi = new MidiObjectPortMidi(config,app);
#endif
#ifdef __OSSMIDI__
  midi = new MidiObjectOSS(config,app);
#endif

  // Instantiate a ControlObject, and set the static midi and config pointer
  control = new ControlNull();
  control->midi = midi;
  control->config = config;

  // Initialize player with a desired buffer size
  qDebug("Init player...");
#ifdef __ALSA__
  player = new PlayerALSA(BUFFER_SIZE, &engines);
#else
  player = new PlayerPortAudio(BUFFER_SIZE, &engines);
#endif

  // Install event handler to update playpos slider and force screen update.
  // This method is used to avoid emitting signals (and QApplication::lock())
  // in the player thread. This ensures that the player will not lock because
  // of a (temporary) stalled GUI thread.
  installEventFilter(this);

  // Start engine
  engineStart();
}

MixxxApp::~MixxxApp()
{
    engineStop();
    delete player;
    delete control;
    delete midi;
    delete playSelectMenu;
}

bool MixxxApp::eventFilter(QObject *o, QEvent *e)
{
    // If a user event is received, update playpos sliders,
    // and force screen update
    if (e->type() == QEvent::User)
    {
        view->playcontrol1->SliderPosition->setValue(buffer1->playposSliderNew);
        view->playcontrol2->SliderPosition->setValue(buffer2->playposSliderNew);

        // Force update
        app->flush();

        return TRUE;
    } else {
        // standard event processing
        return QWidget::eventFilter(o,e);
    }
}

void MixxxApp::engineStart()
{
    if (view->playlist->ListPlaylist->firstChild() != 0)
    {
        qDebug("Init buffer 1... %s", view->playlist->ListPlaylist->firstChild()->text(1).ascii());
        buffer1 = new EngineBuffer(app, this, view->playcontrol1, "[Channel1]", view->playlist->ListPlaylist->firstChild()->text(1));
    } else
        buffer1 = new EngineBuffer(app, this, view->playcontrol1, "[Channel1]", 0);

    if (view->playlist->ListPlaylist->firstChild()->nextSibling() != 0)
    {
        qDebug("Init buffer 2... %s", view->playlist->ListPlaylist->firstChild()->nextSibling()->text(1).ascii());
        buffer2 = new EngineBuffer(app, this, view->playcontrol2, "[Channel2]", view->playlist->ListPlaylist->firstChild()->nextSibling()->text(1));
    } else
        buffer2 = new EngineBuffer(app, this, view->playcontrol2, "[Channel2]", 0);

    qDebug("...");
    channel1 = new EngineChannel(view->channel1, "[Channel1]");
    channel2 = new EngineChannel(view->channel2, "[Channel2]");

    qDebug("Init master...");
    master = new EngineMaster(view->master, view->crossfader, buffer1, buffer2, channel1, channel2, "[Master]");

    /** Connect signals from option menu, selecting processing of left and right channel, to
        EngineMaster */
    connect(optionsLeft, SIGNAL(toggled(bool)), master, SLOT(slotChannelLeft(bool)));
    connect(optionsRight, SIGNAL(toggled(bool)), master, SLOT(slotChannelRight(bool)));
    master->slotChannelLeft(optionsLeft->isOn());
    master->slotChannelRight(optionsRight->isOn());

    qDebug("Starting buffers...");
    buffer1->start();
    buffer2->start();

    // Start audio
    qDebug("Starting player...");
    player->start(master);
}

void MixxxApp::engineStop()
{
    player->stop();
    delete buffer1;
    delete buffer2;
    delete channel1;
    delete channel2;
    delete master;
}

/** initializes all QActions of the application */
void MixxxApp::initActions(){

  QPixmap openIcon, saveIcon, newIcon;
  newIcon = QPixmap(filenew);
  openIcon = QPixmap(fileopen);
  saveIcon = QPixmap(filesave);


  fileNew = new QAction(tr("New File"), newIcon, tr("&New"), QAccel::stringToKey(tr("Ctrl+N")), this);
  fileNew->setStatusTip(tr("Creates a new document"));
  fileNew->setWhatsThis(tr("New File\n\nCreates a new document"));
  connect(fileNew, SIGNAL(activated()), this, SLOT(slotFileNew()));

  fileOpen = new QAction(tr("Open File"), openIcon, tr("&Open..."), 0, this);
  fileOpen->setStatusTip(tr("Opens an existing document"));
  fileOpen->setWhatsThis(tr("Open File\n\nOpens an existing document"));
  connect(fileOpen, SIGNAL(activated()), this, SLOT(slotFileOpen()));

  fileSave = new QAction(tr("Save File"), saveIcon, tr("&Save"), QAccel::stringToKey(tr("Ctrl+S")), this);
  fileSave->setStatusTip(tr("Saves the actual document"));
  fileSave->setWhatsThis(tr("Save File.\n\nSaves the actual document"));
  connect(fileSave, SIGNAL(activated()), this, SLOT(slotFileSave()));

  fileSaveAs = new QAction(tr("Save File As"), tr("Save &as..."), 0, this);
  fileSaveAs->setStatusTip(tr("Saves the actual document under a new filename"));
  fileSaveAs->setWhatsThis(tr("Save As\n\nSaves the actual document under a new filename"));
  connect(fileSaveAs, SIGNAL(activated()), this, SLOT(slotFileSave()));

  fileClose = new QAction(tr("Close File"), tr("&Close"), QAccel::stringToKey(tr("Ctrl+W")), this);
  fileClose->setStatusTip(tr("Closes the actual document"));
  fileClose->setWhatsThis(tr("Close File\n\nCloses the actual document"));
  connect(fileClose, SIGNAL(activated()), this, SLOT(slotFileClose()));

  filePrint = new QAction(tr("Print File"), tr("&Print"), QAccel::stringToKey(tr("Ctrl+P")), this);
  filePrint->setStatusTip(tr("Prints out the actual document"));
  filePrint->setWhatsThis(tr("Print File\n\nPrints out the actual document"));
  connect(filePrint, SIGNAL(activated()), this, SLOT(slotFilePrint()));

  fileQuit = new QAction(tr("Exit"), tr("E&xit"), QAccel::stringToKey(tr("Ctrl+Q")), this);
  fileQuit->setStatusTip(tr("Quits the application"));
  fileQuit->setWhatsThis(tr("Exit\n\nQuits the application"));
  connect(fileQuit, SIGNAL(activated()), this, SLOT(slotFileQuit()));

  editCut = new QAction(tr("Cut"), tr("Cu&t"), QAccel::stringToKey(tr("Ctrl+X")), this);
  editCut->setStatusTip(tr("Cuts the selected section and puts it to the clipboard"));
  editCut->setWhatsThis(tr("Cut\n\nCuts the selected section and puts it to the clipboard"));
  connect(editCut, SIGNAL(activated()), this, SLOT(slotEditCut()));

  editCopy = new QAction(tr("Copy"), tr("&Copy"), QAccel::stringToKey(tr("Ctrl+C")), this);
  editCopy->setStatusTip(tr("Copies the selected section to the clipboard"));
  editCopy->setWhatsThis(tr("Copy\n\nCopies the selected section to the clipboard"));
  connect(editCopy, SIGNAL(activated()), this, SLOT(slotEditCopy()));

  editPaste = new QAction(tr("Paste"), tr("&Paste"), QAccel::stringToKey(tr("Ctrl+V")), this);
  editPaste->setStatusTip(tr("Pastes the clipboard contents to actual position"));
  editPaste->setWhatsThis(tr("Paste\n\nPastes the clipboard contents to actual position"));
  connect(editPaste, SIGNAL(activated()), this, SLOT(slotEditPaste()));

  optionsLeft = new QAction(tr("Left channel"), tr("&Left channel"), QAccel::stringToKey(tr("Ctrl+L")), this, 0, true);
  optionsLeft->setOn(true);
  optionsRight = new QAction(tr("Right channel"), tr("&Right channel"), QAccel::stringToKey(tr("Ctrl+R")), this, 0, true);
  optionsRight->setOn(true);

  optionsPreferences = new QAction(tr("Preferences"), tr("&Preferences..."), 0, this);
  optionsPreferences->setStatusTip(tr("Preferences"));
  optionsPreferences->setWhatsThis(tr("Preferences\nPlayback and MIDI preferences"));
  connect(optionsPreferences, SIGNAL(activated()), this, SLOT(slotOptionsPreferences()));

/*
  viewToolBar = new QAction(tr("Toolbar"), tr("Tool&bar"), 0, this, 0, true);
  viewToolBar->setStatusTip(tr("Enables/disables the toolbar"));
  viewToolBar->setWhatsThis(tr("Toolbar\n\nEnables/disables the toolbar"));
  connect(viewToolBar, SIGNAL(toggled(bool)), this, SLOT(slotViewToolBar(bool)));
*/

/*
  viewStatusBar = new QAction(tr("Statusbar"), tr("&Statusbar"), 0, this, 0, true);
  viewStatusBar->setStatusTip(tr("Enables/disables the statusbar"));
  viewStatusBar->setWhatsThis(tr("Statusbar\n\nEnables/disables the statusbar"));
  connect(viewStatusBar, SIGNAL(toggled(bool)), this, SLOT(slotViewStatusBar(bool)));
*/
  helpAboutApp = new QAction(tr("About"), tr("&About..."), 0, this);
  helpAboutApp->setStatusTip(tr("About the application"));
  helpAboutApp->setWhatsThis(tr("About\n\nAbout the application"));
  connect(helpAboutApp, SIGNAL(activated()), this, SLOT(slotHelpAbout()));

}

void MixxxApp::initMenuBar()
{
  ///////////////////////////////////////////////////////////////////
  // MENUBAR

  ///////////////////////////////////////////////////////////////////
  // menuBar entry fileMenu
  fileMenu=new QPopupMenu();
  fileNew->addTo(fileMenu);
  fileOpen->addTo(fileMenu);
  fileClose->addTo(fileMenu);
  fileMenu->insertSeparator();
  fileSave->addTo(fileMenu);
  fileSaveAs->addTo(fileMenu);
  fileMenu->insertSeparator();
  filePrint->addTo(fileMenu);
  fileMenu->insertSeparator();
  fileQuit->addTo(fileMenu);

  ///////////////////////////////////////////////////////////////////
  // menuBar entry editMenu
  editMenu=new QPopupMenu();
  editCut->addTo(editMenu);
  editCopy->addTo(editMenu);
  editPaste->addTo(editMenu);

  ///////////////////////////////////////////////////////////////////
  // menuBar entry optionsMenu
  optionsMenu=new QPopupMenu();
  optionsMenu->setCheckable(true);
  optionsLeft->addTo(optionsMenu);
  optionsRight->addTo(optionsMenu);
  optionsMenu->insertSeparator();
  optionsPreferences->addTo(optionsMenu);

  ///////////////////////////////////////////////////////////////////
  // menuBar entry viewMenu
  viewMenu=new QPopupMenu();
  viewMenu->setCheckable(true);
  //viewToolBar->addTo(viewMenu);
  //viewStatusBar->addTo(viewMenu);
  ///////////////////////////////////////////////////////////////////
  // EDIT YOUR APPLICATION SPECIFIC MENUENTRIES HERE

  ///////////////////////////////////////////////////////////////////
  // menuBar entry helpMenu
  helpMenu=new QPopupMenu();
  helpAboutApp->addTo(helpMenu);

  ///////////////////////////////////////////////////////////////////
  // MENUBAR CONFIGURATION
  menuBar()->insertItem(tr("&File"), fileMenu);
  menuBar()->insertItem(tr("&Edit"), editMenu);
  menuBar()->insertItem(tr("&Options"), optionsMenu);
  //menuBar()->insertItem(tr("&View"), viewMenu);
  menuBar()->insertSeparator();
  menuBar()->insertItem(tr("&Help"), helpMenu);

}

void MixxxApp::initToolBar()
{
  ///////////////////////////////////////////////////////////////////
  // TOOLBAR
/*
  fileToolbar = new QToolBar(this, "file operations");
  fileNew->addTo(fileToolbar);
  fileOpen->addTo(fileToolbar);
  fileSave->addTo(fileToolbar);
  fileToolbar->addSeparator();
  QWhatsThis::whatsThisButton(fileToolbar);
*/
}

void MixxxApp::initStatusBar()
{
  ///////////////////////////////////////////////////////////////////
  //STATUSBAR
  statusBar()->message(tr("Ready."), 2000);
}

void MixxxApp::initDoc()
{
   doc=new MixxxDoc();
}

void MixxxApp::initView()
{
  ////////////////////////////////////////////////////////////////////
  // set the main widget here
  view=new MixxxView(this, doc);
  setCentralWidget(view);
}

bool MixxxApp::queryExit()
{
  int exit=QMessageBox::information(this, tr("Quit..."),
                                    tr("Do your really want to quit?"),
                                    QMessageBox::Ok, QMessageBox::Cancel);

  if (exit==1)
  {

  }
  else
  {

  };

  return (exit==1);
}

/////////////////////////////////////////////////////////////////////
// SLOT IMPLEMENTATION
/////////////////////////////////////////////////////////////////////


void MixxxApp::slotFileNew()
{
  statusBar()->message(tr("Creating new file..."));
  doc->newDoc();
  statusBar()->message(tr("Ready."));
}

void MixxxApp::slotFileOpen()
{
  statusBar()->message(tr("Opening file..."));

  QString fileName = QFileDialog::getOpenFileName(0,0,this);
  if (!fileName.isEmpty())
  {
    doc->load(fileName);
    setCaption(fileName);
    QString message=tr("Loaded document: ")+fileName;
    statusBar()->message(message, 2000);
  }
  else
  {
    statusBar()->message(tr("Opening aborted"), 2000);
  }
}


void MixxxApp::slotFileSave()
{
  statusBar()->message(tr("Saving file..."));
  doc->save();
  statusBar()->message(tr("Ready."));
}

void MixxxApp::slotFileSaveAs()
{
  statusBar()->message(tr("Saving file under new filename..."));
  QString fn = QFileDialog::getSaveFileName(0, 0, this);
  if (!fn.isEmpty())
  {
    doc->saveAs(fn);
  }
  else
  {
    statusBar()->message(tr("Saving aborted"), 2000);
  }

  statusBar()->message(tr("Ready."));
}

void MixxxApp::slotFileClose()
{
  statusBar()->message(tr("Closing file..."));

  statusBar()->message(tr("Ready."));
}

void MixxxApp::slotFilePrint()
{
  statusBar()->message(tr("Printing..."));
  QPrinter printer;
  if (printer.setup(this))
  {
    QPainter painter;
    painter.begin(&printer);

    ///////////////////////////////////////////////////////////////////
    // TODO: Define printing by using the QPainter methods here

    painter.end();
  };

  statusBar()->message(tr("Ready."));
}

void MixxxApp::slotFileQuit()
{
  statusBar()->message(tr("Exiting application..."));

  ///////////////////////////////////////////////////////////////////
  // exits the Application
  if(doc->isModified())
  {
    if(queryExit())
    {
      qApp->quit();
    }
    else
    {

    };
  }
  else
  {
    qApp->quit();
  };

  statusBar()->message(tr("Ready."));
}

void MixxxApp::slotEditCut()
{
  statusBar()->message(tr("Cutting selection..."));

  statusBar()->message(tr("Ready."));
}

void MixxxApp::slotEditCopy()
{
  statusBar()->message(tr("Copying selection to clipboard..."));

  statusBar()->message(tr("Ready."));
}

void MixxxApp::slotEditPaste()
{
  statusBar()->message(tr("Inserting clipboard contents..."));

  statusBar()->message(tr("Ready."));
}

void MixxxApp::slotViewToolBar(bool toggle)
{
  statusBar()->message(tr("Toggle toolbar..."));
  ///////////////////////////////////////////////////////////////////
  // turn Toolbar on or off

  if (toggle== false)
  {
//    fileToolbar->hide();
  }
  else
  {
//    fileToolbar->show();
  };

  statusBar()->message(tr("Ready."));
}

void MixxxApp::slotViewStatusBar(bool toggle)
{
  statusBar()->message(tr("Toggle statusbar..."));
  ///////////////////////////////////////////////////////////////////
  //turn Statusbar on or off

  if (toggle == false)
  {
    statusBar()->hide();
  }
  else
  {
    statusBar()->show();
  }

  statusBar()->message(tr("Ready."));
}

void MixxxApp::slotOptionsPreferences()
{
    if (pDlg==0)
    {
        pDlg = new DlgPreferences(this);
        QPtrList<Player::Info> *pInfo = player->getInfo();

        // Fill dialog with info

        // Sound card info
        for (unsigned int j=0; j<pInfo->count(); j++)
        {
            Player::Info *p = pInfo->at(j);
            // Name of device
            pDlg->ComboBoxSoundcard->insertItem(p->name);
            if (p->name == player->NAME)
            {
                pDlg->ComboBoxSoundcard->setCurrentItem(j);
                slotOptionsPreferencesUpdateDeviceOptions();
            }
        }

        // Midi configuration
        QStringList *list = configMap->getConfigurations();
        QStringList::Iterator it = list->begin();
        for (; it != list->end(); ++it)
            pDlg->ComboBoxMidiconf->insertItem(*it);

        // Midi device
        QStringList *mididev = midi->getDeviceList();
        int j=0;
        for (it = mididev->begin(); it != mididev->end(); ++it )
        {
            pDlg->ComboBoxMididevice->insertItem(*it);
            if ((*it) == (*midi->getOpenDevice()))
                pDlg->ComboBoxMididevice->setCurrentItem(j);
            j++;
        }

        // Connect buttons
        connect(pDlg->PushButtonOK,      SIGNAL(clicked()),      this, SLOT(slotOptionsSetPreferences()));
        connect(pDlg->PushButtonCancel,  SIGNAL(clicked()),      this, SLOT(slotOptionsClosePreferences()));
        connect(pDlg->ComboBoxSoundcard, SIGNAL(activated(int)), this, SLOT(slotOptionsPreferencesUpdateDeviceOptions()));

        // Show dialog
        pDlg->show();
    }
}

void MixxxApp::slotOptionsPreferencesUpdateDeviceOptions()
{
    QPtrList<Player::Info> *pInfo = player->getInfo();
    Player::Info *p = pInfo->first();

    while (p != 0)
    {
        if (pDlg->ComboBoxSoundcard->currentText() == p->name)
        {
            // Sample rates
            pDlg->ComboBoxSamplerates->clear();
            for (unsigned int i=0; i<p->sampleRates.size(); i++)
            {
                pDlg->ComboBoxSamplerates->insertItem(QString("%1 Hz").arg(p->sampleRates[i]));
                if (p->sampleRates[i]==player->SRATE)
                    pDlg->ComboBoxSamplerates->setCurrentItem(i);
            }

            // Bits
            pDlg->ComboBoxBits->clear();
            for (unsigned int i=0; i<p->bits.size(); i++)
            {
                pDlg->ComboBoxBits->insertItem(QString("%1").arg(p->bits[i]));
                if (p->bits[i]==player->BITS)
                    pDlg->ComboBoxBits->setCurrentItem(i);
            }
        }

        // Get next device
        p = pInfo->next();
    }
}

void MixxxApp::slotOptionsSetPreferences()
{
/*
    // Show warning dialog box
    switch( QMessageBox::information( this, "Mixxx",
        "For the changes to take effect,\nthe sound will stop.",
        "&OK", "&Cancel",
        0, 2))  // Enter == button 0, Escape == button 2
    {
    case 0: // Ok clicked or Alt+O pressed or Enter pressed.
        break;
    case 1: // Cancel clicked or Alt+C pressed or Escape pressed
        slotOptionsClosePreferences();
        return;
    }
*/

    // Get parameters from dialog
    QString name = pDlg->ComboBoxSoundcard->currentText();

    QString temp = pDlg->ComboBoxSamplerates->currentText();
    temp.truncate(temp.length()-3);
    int srate = temp.toInt();

    temp = pDlg->ComboBoxBits->currentText();
    temp.truncate(temp.length()-3);
    int bits = temp.toInt();

    int bufferSize = BUFFER_SIZE;

    // Perform changes to sound card setup
    player->stop();
    player->reopen(name,srate,bits,bufferSize);
    player->start(master);

    // Perform changes to MIDI configuration
    configMap->setConfiguration(pDlg->ComboBoxMidiconf->currentText());

    // Change MIDI device
    //std::raise(2);
    midi->reopen(pDlg->ComboBoxMididevice->currentText());

    slotOptionsClosePreferences();
}

void MixxxApp::slotOptionsClosePreferences()
{
    delete pDlg;
    pDlg = 0;
}

void MixxxApp::slotHelpAbout()
{
  QMessageBox::about(this,tr("About..."),
                      tr("Mixxx\nVersion " VERSION "\n(c) 2002 by Tue and Ken Haste Andersen") );
}

void MixxxApp::slotChangePlay_1()
{
    buffer1->newtrack(selection.ascii());
}

void MixxxApp::slotChangePlay_2()
{
    buffer2->newtrack(selection.ascii());
}

void MixxxApp::slotSelectPlay(QListViewItem *item, const QPoint &pos, int)
{
    // Store the current selected track
    selection = item->text(1);

    // Display popup menu
    playSelectMenu->popup(pos);
}
