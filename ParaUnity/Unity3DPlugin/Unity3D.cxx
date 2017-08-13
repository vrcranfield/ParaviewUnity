#include "Unity3D.h"

// Actions
#define UNITY_PLAYER_ACTION "UNITY_PLAYER_ACTION"
#define UNITY_EDITOR_ACTION "UNITY_EDITOR_ACTION"

/**
* Default constructor.
*/
Unity3D::Unity3D(QObject *p) : QActionGroup(p), unityPlayerProcess(NULL) {
	// Set working directory
	this->workingDir = QDir::tempPath() + "/Unity3DPlugin";

	// Create directory if it doesn't exist
	if (!dirExists(this->workingDir.toStdString())) {
		CreateDirectory(this->workingDir.toStdString().c_str(), NULL);
	}

	// Set up player mode
	QIcon embeddedActionIcon(QPixmap(":/Unity3D/resources/player.png"));
	embeddedActionIcon.addPixmap(
		QPixmap(":/Unity3D/resources/player_selected.png"),
		QIcon::Mode::Selected);
	QAction *embeddedAction =
		new QAction(embeddedActionIcon, "Show in Unity Player", this);
	embeddedAction->setData(UNITY_PLAYER_ACTION);
	this->addAction(embeddedAction);

	// Set up editor mode
	QIcon exportActionIcon(QPixmap(":/Unity3D/resources/editor.png"));
	exportActionIcon.addPixmap(QPixmap(":/Unity3D/resources/editor_selected.png"),
														 QIcon::Mode::Selected);
	QAction *exportAction =
		new QAction(exportActionIcon, "Export to Unity Editor", this);
	exportAction->setData(UNITY_EDITOR_ACTION);
	this->addAction(exportAction);

	// Instantiate a new X3D exporter
	this->exporter = vtkX3DExporter::New();

	// Set the name of the object for Named Shared Memory
	this->objectName = "ParaviewOutput";

	// Connect button press to callback
	QObject::connect(this, SIGNAL(triggered(QAction *)), this,
									 SLOT(onAction(QAction *)));
}

/**
* Switch for button press event callbacks
*/
void Unity3D::onAction(QAction *a) {
	pqApplicationCore *core = pqApplicationCore::instance();
	pqServerManagerModel *sm = core->getServerManagerModel();

	if (sm->getNumberOfItems<pqServer *>()) {
		if (a->data() == UNITY_PLAYER_ACTION) {
			this->exportToUnityPlayer(sm);
		}
		else if (a->data() == UNITY_EDITOR_ACTION) {
			this->exportToUnityEditor(sm);
		}
		else {
			qFatal("Unexpected action type\n");
		}
	}
}

/**
* Callback for Player button pressed
*/
void Unity3D::exportToUnityPlayer(pqServerManagerModel *sm) {
	// If no player is running, instantiate one
	if (this->unityPlayerProcess == NULL ||
			this->unityPlayerProcess->pid() <= 0) {
		this->unityPlayerProcess = new QProcess(this);

		QString processBinary = getUnityPlayerBinary(this->workingDir);
		this->unityPlayerProcess->start(processBinary);
		if (!unityPlayerProcess->waitForStarted()) {
			QMessageBox::critical(NULL, tr("Unity Player Error"),
														tr("Player process could not be executed"));
			return;
		}

		// Set appropriate location
		QString portFileRoot(this->workingDir + "/Embedded/");

		// Create directory if it doesn't exist
		if (!dirExists(portFileRoot.toStdString())) {
			CreateDirectory(portFileRoot.toStdString().c_str(), NULL);
		}

		// Set working directory
		this->playerWorkingDir = portFileRoot +
			QString::number(getProcessID(this->unityPlayerProcess));

		// Find port file
		this->port = findPortFile(playerWorkingDir);
	}

	// Export scene
	exportScene(sm, this->port);
}

/**
* Callback for Editor button pressed
*/
void Unity3D::exportToUnityEditor(pqServerManagerModel *sm) {
	// Set appropriate location
	QString portFileRoot(this->workingDir + "/Editor");

	// Create directory if it doesn't exist
	if (!dirExists(portFileRoot.toStdString())) {
		CreateDirectory(portFileRoot.toStdString().c_str(), NULL);
	}

	// Find active Unity instances
	QList<int> activeUnityInstances;
	foreach(const QString &dir, QDir(portFileRoot).entryList()) {
		if (dir != "." && dir != "..") {
			int port = dir.toInt();

			// Check if active
			if (pollClient(port)) {
				activeUnityInstances << port;
			}
			else {
				removeDir(portFileRoot + "/" + dir);
			}
		}
	}

	if (activeUnityInstances.isEmpty()) {
		QMessageBox::critical(NULL, tr("Unity Editor not running"),
													tr("Start a prepared Unity project first"));
	}
	else if (activeUnityInstances.length() > 1) {
		QMessageBox::critical(NULL, tr("Error"),
													tr("Multiple Unity instances are running at the same time"));
	}
	else {
		// Save port number of active instance
		this->port = activeUnityInstances[0];

		// Export scene
		exportScene(sm, this->port);
	}
}

/**
* Exports the scene to Shared Memory
*/
void Unity3D::exportScene(pqServerManagerModel *sm, int port) {
	
	// Attach to ParaView scene
	QList<pqRenderView *> renderViews = sm->findItems<pqRenderView *>();
	this->renderProxy = renderViews[0]->getRenderViewProxy();
	pqPVApplicationCore *core = pqPVApplicationCore::instance();
	pqAnimationScene *scene = core->animationManager()->getActiveScene();

	// Write to a field in the exporter
	exporter->SetWriteToOutputString(true);

	// Get total number of frames
	this->totalFrames = scene->getTimeSteps().length();

	// If there is an animation
	if (totalFrames > 0) {
		LogDebug("Exporting animation of frames: " + QString::number(totalFrames));
		vtkSMPropertyHelper animationProp(scene->getProxy(), "AnimationTime");
		exportFirstFrame();
	}
	
	// If there is no animation
	else {
		exporter->SetInput(renderProxy->GetRenderWindow());
		exporter->Write();
	}

	// Set object size
	this->objectSize = exporter->GetOutputStringLength() * sizeof(char);
	
	// Write to shared memory
	writeSceneToMemory();

	// Compose message
	QString message(objectName + QString(";;") + QString::number(objectSize));

	if (totalFrames > 0)
		message += QString(";;") + QString::number(this->lastExportedFrame) + QString(";;") + QString::number(this->totalFrames);

	// Send message
	if (!sendMessageExpectingReply(message, port)) {
		QMessageBox::critical(NULL, tr("Unity Error"),
													tr("Unable to communicate to Unity process"));
	}
}

/**
* Writes the scene exported as X3D in Shared Memory
*/
void Unity3D::writeSceneToMemory() {
	// Create file mapping
	this->handle = CreateFileMapping(
		INVALID_HANDLE_VALUE,			// use paging file
		NULL,											// default security
		PAGE_READWRITE,						// read/write access
		0,												// maximum object size (high-order DWORD)
		objectSize,								// maximum object size (low-order DWORD)
		objectName);							// name of mapping object

	if (!handle || handle == INVALID_HANDLE_VALUE)
	{
		QMessageBox::critical(NULL, tr("Unity Error"),
													tr(std::string("Could not create file mapping object: " + GetLastError()).c_str()));
		return;
	}

	// Map a view of the file in process space
	pBuf = (char *)MapViewOfFile(handle,							// handle to map object
															 FILE_MAP_ALL_ACCESS, // read/write permission
															 0,										// file offset (high-order DWORD)
															 0,										// file offset (high-order DWORD)
															 objectSize);					// size of object in byte

	if (pBuf == NULL)
	{
		QMessageBox::critical(NULL, tr("Unity Error"),
													tr(std::string("Could not map view of file: " + GetLastError()).c_str()));
		CloseHandle(handle);
		return;
	}

	// Copy data to Shared Memory
	CopyMemory((void *)pBuf, exporter->GetOutputString(), objectSize);
}

/**
* Exports the first frame of an animated scene
*/
void Unity3D::exportFirstFrame() {
	// Attach to ParaView scene
	pqPVApplicationCore *core = pqPVApplicationCore::instance();
	pqAnimationScene *scene = core->animationManager()->getActiveScene();
	vtkSMPropertyHelper animationProp(scene->getProxy(), "AnimationTime");

	// Set frame to 0
	animationProp.Set(scene->getTimeSteps()[0]);
	scene->getProxy()->UpdateVTKObjects();

	// Write to exporter
	this->exporter->SetInput(renderProxy->GetRenderWindow());
	this->exporter->Write();

	// Update counter
	this->lastExportedFrame = 0;

	// If it is the last frame
	if (lastExportedFrame == totalFrames - 1) {
		animationProp.Set(animationProp.GetAsDouble());
	}
}

/**
* Exports the next frame of an animated scene
*/
void Unity3D::exportNextFrame() {
	// Attach to ParaView scene
	pqPVApplicationCore *core = pqPVApplicationCore::instance();
	pqAnimationScene *scene = core->animationManager()->getActiveScene();
	vtkSMPropertyHelper animationProp(scene->getProxy(), "AnimationTime");

	// Advance frame
	animationProp.Set(scene->getTimeSteps()[lastExportedFrame + 1]);
	scene->getProxy()->UpdateVTKObjects();

	// Write to exporter
	this->exporter->SetInput(renderProxy->GetRenderWindow());
	this->exporter->Write();

	// Update object size
	this->objectSize = exporter->GetOutputStringLength() * sizeof(char);

	// Write to shared memory
	writeSceneToMemory();

	// Update counter
	lastExportedFrame++;

	// If it is the last frame
	if (lastExportedFrame == totalFrames - 1) {
		animationProp.Set(animationProp.GetAsDouble());
	}
}

/**
* Frees the current object from shared memory 
*/
void Unity3D::freeSharedMemory() {
	if (pBuf != NULL) {
		UnmapViewOfFile(pBuf);
		pBuf = NULL;
	}

	if (handle != NULL) {
		CloseHandle(handle);
		handle = NULL;
	}
}

/**
* Test socket availability
*/
bool Unity3D::pollClient(int port) {

	QTcpSocket *socket = new QTcpSocket(this);

	socket->connectToHost("127.0.0.1", port);

	bool connected = socket->waitForConnected();

	socket->abort();

	return connected;
}

/**
* Send message to client
*/
bool Unity3D::sendMessage(const QString& message, int port) {

	socket = new QTcpSocket(this);
	socket->connectToHost("127.0.0.1", port);

	if (!socket->waitForConnected()) {
		return false;
	}

	LogDebug("Sending message: " + message);

	socket->write(QByteArray(message.toLatin1()));
	socket->waitForBytesWritten();

	return true;
}

/**
* Send message to client and register callback for reply
*/
bool Unity3D::sendMessageExpectingReply(const QString& message, int port) {

	bool result = sendMessage(message, port);

	if (result)
		QObject::connect(socket, SIGNAL(readyRead()), this, SLOT(readyRead()));

	return result;
}

/**
* Callback for available data on socket
*/
void Unity3D::readyRead() {

	QString reply = QString(socket->readAll());

	LogDebug("Received reply: " + reply);

	QRegExp re;
	re.setPattern("^(OK )(\\d+)");

	// If there is no animation
	if (reply.compare(QString("OK")) == 0)
		freeSharedMemory();

	// If there is animation
	else if (re.indexIn(reply) != -1) {
		int lastImportedFrame = re.cap(2).toInt();
		freeSharedMemory();

		// If there are other frames to export
		if (lastImportedFrame < this->totalFrames - 1) {
			exportNextFrame();

			// Build message
			QString message(
				objectName +
				QString(";;") +
				QString::number(objectSize) +
				QString(";;") +
				QString::number(this->lastExportedFrame) +
				QString(";;") +
				QString::number(this->totalFrames)
			);

			if (!sendMessageExpectingReply(message, this->port)) {
				QMessageBox::critical(NULL, tr("Unity Error"),
															tr("Unable to communicate to Unity process"));
			}
		}
	}
}

/**
* Log debug messages to VTK Output window
*/
static void LogDebug(QString message) {
	vtkOutputWindow::GetInstance()->DisplayDebugText(message.toStdString().c_str());
}

/**
* Get process ID of a QProcess
*/
static quint64 getProcessID(const QProcess* proc) {

#ifdef Q_WS_WIN
	struct _PROCESS_INFORMATION* procinfo = proc->pid();
	return procinfo ? procinfo->dwProcessId : 0;
#else // other
	return (quint64)proc->pid();
#endif // Q_WS_WIN
}

/**
* Find Unity player binary in working directory
*/
static QString getUnityPlayerBinary(const QString& workingDir) {
	// Attach to plugin manager
	pqPVApplicationCore* core = pqPVApplicationCore::instance();
	pqPluginManager* pluginManager = core->getPluginManager();
	vtkPVPluginsInformation* localPlugins = pluginManager->loadedExtensions(pqActiveObjects::instance().activeServer(), false);

	QString targetFile = "unity_player.exe";

	// Find file in working directory
	for (unsigned int i = 0; i < localPlugins->GetNumberOfPlugins(); i++) {
		QString pluginName(localPlugins->GetPluginName(i));
		if (pluginName == "Unity3D") {
			QDir pluginDir(localPlugins->GetPluginFileName(i));
			pluginDir.cdUp();
			return pluginDir.absolutePath() + "/" + targetFile;
		}
	}

	qFatal("Unable to resolve plugin location\n");
	return NULL;
}

/**
* Find port number in player working directory
*/
static int getPortNumberFrom(const QString& playerWorkingDir) {
	QFileInfoList files = QDir(playerWorkingDir).entryInfoList();
	foreach(const QFileInfo &file, files) {
		if (!file.isDir() && file.baseName().contains("port")) {
			return file.baseName().mid(4).toInt();
		}
	}
	return 0;
}

/**
* Check if file exists and is not a directory
*/
static bool fileExists(const QString& path) {
	QFileInfo check_file(path);
	return (check_file.exists() && check_file.isFile());
}

/**
* Check if directory exists and is not a file
*/
static bool dirExists(const std::string& dirName_in)
{
	DWORD ftyp = GetFileAttributesA(dirName_in.c_str());
	if (ftyp == INVALID_FILE_ATTRIBUTES)
		return false; 

	if (ftyp & FILE_ATTRIBUTE_DIRECTORY)
		return true;

	return false;
}

/**
* Recursively delete a directory
*/
static bool removeDir(const QString& dirName)
{
	bool result = true;
	QDir dir(dirName);

	if (dir.exists()) {
		foreach(QFileInfo info, dir.entryInfoList(QDir::NoDotAndDotDot
																							| QDir::System | QDir::Hidden | QDir::AllDirs
																							| QDir::Files, QDir::DirsFirst)) {
			if (info.isDir()) {
				result = removeDir(info.absoluteFilePath());
			}
			else {
				result = QFile::remove(info.absoluteFilePath());
			}

			if (!result) {
				return result;
			}
		}
		result = QDir().rmdir(dirName);
	}
	return result;
}

/**
* Wait for Unity initialization and then find port file.
*/
static int findPortFile(const QString& playerWorkingDir) {
	QWidget *window = QApplication::activeWindow();
	LoadingSplashScreen splashScreen(":/Unity3D/resources/loader.gif");
	int port = 0;

	window->setEnabled(false);
	splashScreen.show();

	// Wait for Unity initialization
	QTime nextCheck = QTime::currentTime().addSecs(1);
	do {
		if (QTime::currentTime() > nextCheck) {
			port = getPortNumberFrom(playerWorkingDir);
			nextCheck = QTime::currentTime().addSecs(1);
		}
		QApplication::instance()->processEvents();
	} while (port == 0);

	// File found
	splashScreen.hide();
	window->setEnabled(true);
	return port;
}

