// SceneEdit.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "SceneEdit.h"

#include "MainFrm.h"
#include "ChildFrm.h"
#include "SceneDoc.h"
#include "SceneView.h"
#include "OptionsDlg.h"
#include "../mfc_shared/utils_mfc.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#ifdef WME_D3D9
#	if _DEBUG
#		pragma comment(lib, "wme_ad_d3d9_d.lib")
#	else
#		pragma comment(lib, "wme_ad_d3d9.lib")
#	endif
#else
#	if _DEBUG
#		pragma comment(lib, "wme_ad_d.lib")
#	else
#		pragma comment(lib, "wme_ad.lib")
#	endif
#endif


/////////////////////////////////////////////////////////////////////////////
// CSceneEditApp

BEGIN_MESSAGE_MAP(CSceneEditApp, CWinApp)
	//{{AFX_MSG_MAP(CSceneEditApp)
	ON_COMMAND(ID_APP_ABOUT, OnAppAbout)
	ON_COMMAND(ID_TOOLS_OPTIONS, OnToolsOptions)
	//}}AFX_MSG_MAP
	// Standard file based document commands
	ON_COMMAND(ID_FILE_NEW, CWinApp::OnFileNew)
	ON_COMMAND(ID_FILE_OPEN, CWinApp::OnFileOpen)
	// Standard print setup command
	ON_COMMAND(ID_FILE_PRINT_SETUP, CWinApp::OnFilePrintSetup)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CSceneEditApp construction

CSceneEditApp::CSceneEditApp() :
	CBCGWorkspace (TRUE /* m_bResourceSmartUpdate */)
{
	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
}

/////////////////////////////////////////////////////////////////////////////
// The one and only CSceneEditApp object

CSceneEditApp theApp;

/////////////////////////////////////////////////////////////////////////////
// CSceneEditApp initialization

BOOL CSceneEditApp::InitInstance()
{
	// Initialize OLE libraries
	if (!AfxOleInit())
	{
		AfxMessageBox(IDP_OLE_INIT_FAILED);
		return FALSE;
	}

	char CurrentDir[MAX_PATH+1];
	GetCurrentDirectory(MAX_PATH, CurrentDir);
	AddProjectRoot(CString(CurrentDir));


	AfxEnableControlContainer();

	// Standard initialization
	// If you are not using these features and wish to reduce the size
	//  of your final executable, you should remove from the following
	//  the specific initialization routines you do not need.


	// Change the registry key under which our settings are stored.
	// TODO: You should modify this string to be something appropriate
	// such as the name of your company or organization.
	SetRegistryKey(_T("DEAD:CODE"));

	LoadStdProfileSettings(8);  // Load standard INI file options (including MRU)

	SetRegistryBase (_T("Settings"));

	// Initialize all Managers for usage. They are automatically constructed
	// if not yet present
	InitContextMenuManager();
	InitKeyboardManager();

	// Enable user-defined tools. If you want allow more than 10 tools,
	// add tools entry to resources (ID_USER_TOOL11, ID_USER_TOOL12,...)
	EnableUserTools (ID_TOOLS_ENTRY, ID_USER_TOOL1, ID_USER_TOOL10);

	// Register the application's document templates.  Document templates
	//  serve as the connection between documents, frame windows and views.

	CMultiDocTemplate* pDocTemplate;
	pDocTemplate = new CMultiDocTemplate(
		IDR_SCENETYPE,
		RUNTIME_CLASS(CSceneDoc),
		RUNTIME_CLASS(CChildFrame), // custom MDI child frame
		RUNTIME_CLASS(CSceneView));
	AddDocTemplate(pDocTemplate);

	// create main MDI Frame window
	CMainFrame* pMainFrame = new CMainFrame;
	if (!pMainFrame->LoadFrame(IDR_MAINFRAME))
		return FALSE;
	m_pMainWnd = pMainFrame;

	// Enable drag/drop open
	m_pMainWnd->DragAcceptFiles();

	// Enable DDE Execute open
	//EnableShellOpen();
	//RegisterShellFileTypes(TRUE);

	// Parse command line for standard shell commands, DDE, file open
	CCommandLineInfo cmdInfo;
	ParseCommandLine(cmdInfo);
	if(cmdInfo.m_nShellCommand==CCommandLineInfo::FileNew) cmdInfo.m_nShellCommand = CCommandLineInfo::FileNothing;

	// Dispatch commands specified on the command line
	if (!ProcessShellCommand(cmdInfo))
		return FALSE;


	// The main window has been initialized, so show and update it.
	pMainFrame->ShowWindow(m_nCmdShow);
	pMainFrame->UpdateWindow();

	return TRUE;
}

/////////////////////////////////////////////////////////////////////////////
// CSceneEditApp message handlers

int CSceneEditApp::ExitInstance() 
{
	BCGCBCleanUp();

	return CWinApp::ExitInstance();
}


/////////////////////////////////////////////////////////////////////////////
// CAboutDlg dialog used for App About

class CAboutDlg : public CDialog
{
public:
	CAboutDlg();

// Dialog Data
	//{{AFX_DATA(CAboutDlg)
	enum { IDD = IDD_ABOUTBOX };
	CBCGURLLinkButton	m_btnWeb;
	CBCGURLLinkButton	m_btnURL;
	CStatic	m_Product;	
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAboutDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	//{{AFX_MSG(CAboutDlg)
	virtual BOOL OnInitDialog();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD)
{
	//{{AFX_DATA_INIT(CAboutDlg)
	//}}AFX_DATA_INIT
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAboutDlg)
	DDX_Control(pDX, IDC_COMPANY_WEB, m_btnWeb);
	DDX_Control(pDX, IDC_COMPANY_URL, m_btnURL);
	DDX_Control(pDX, IDC_PRODUCT, m_Product);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
	//{{AFX_MSG_MAP(CAboutDlg)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

// App command to run the dialog
void CSceneEditApp::OnAppAbout()
{
	CAboutDlg aboutDlg;
	aboutDlg.DoModal();
}


void CSceneEditApp::PreLoadState ()
{

	GetContextMenuManager()->AddMenu (_T("My menu"), IDR_CONTEXT_MENU);
	GetContextMenuManager()->AddMenu (_T("Scale level menu"), IDR_POPUP_SCALE);
	GetContextMenuManager()->AddMenu (_T("Rotation level menu"), IDR_POPUP_ROTATION);

	// TODO: add another context menus here
}


/////////////////////////////////////////////////////////////////////////////
// CSceneEditApp message handlers

BOOL CSceneEditApp::OnIdle(LONG lCount) 
{
	if(m_Active){
		CMDIFrameWnd *pFrame = (CMDIFrameWnd*)AfxGetApp()->m_pMainWnd;
		if(pFrame==NULL) return FALSE;

		// Get the active MDI child window.
		CMDIChildWnd *pChild = (CMDIChildWnd *) pFrame->GetActiveFrame();
		if(pChild==NULL) return FALSE;

		CView *pView = pChild->GetActiveView();
		if(pView==NULL) return FALSE;
		pView->Invalidate();	

	

		CWinApp::OnIdle(lCount);
		return TRUE;
	}
	return CWinApp::OnIdle(lCount);
}


//////////////////////////////////////////////////////////////////////////
BOOL CAboutDlg::OnInitDialog() 
{
	CDialog::OnInitDialog();
	
	m_btnURL.SetURLPrefix (_T("mailto:"));
	m_btnURL.SetURL (_T("wme@wintermute-engine.org?subject=SceneEdit"));
	m_btnURL.SizeToContent();
	m_btnWeb.SetURL (_T("http://www.wintermute-engine.org/"));
	m_btnWeb.SizeToContent();

	CString str;
	m_Product.GetWindowText(str);
	str.Format("%s  %d.%d.%03d", str, DCGF_VER_MAJOR, DCGF_VER_MINOR, DCGF_VER_BUILD);
	m_Product.SetWindowText(str);	

	return TRUE;
}


//////////////////////////////////////////////////////////////////////////
void CSceneEditApp::OnToolsOptions() 
{
	// TODO: Add your command handler code here
	COptionsDlg dlg;
	dlg.DoModal();
	
}


//////////////////////////////////////////////////////////////////////////
void CSceneEditApp::AddProjectRoot(CString Folder)
{
	if(Folder=="") return;

	Folder.Replace('/', '\\');

	if(Folder[Folder.GetLength()-1]!='\\') Folder+='\\';

	for(int i=0; i<m_ProjectRoots.GetSize(); i++){
		if(m_ProjectRoots[i]==Folder) return;
	}
	m_ProjectRoots.Add(Folder);
}


//////////////////////////////////////////////////////////////////////////
bool CSceneEditApp::EditFile(CString Filename, CBGame *Game, CWnd* Parent)
{
	if(!Game) return false;

	HWND win;
	if(Parent) win = Parent->GetSafeHwnd();
	else win = NULL;

	CString EditFile = Filename;

	// make an absolute path
	EditFile = MakeAbsolutePath(Filename, Game);

	if(EditFile!=""){
		m_Active = false;
		if((int)ShellExecute(win, "edit", EditFile, "", "", SW_NORMAL)<=32)
			if((int)ShellExecute(win, "open", EditFile, "", "", SW_NORMAL)<=32){
				
				char drive[_MAX_DRIVE];
				char dir[_MAX_DIR];
				CString ExploreDir;
				_splitpath(EditFile, drive, dir, NULL, NULL);
				ExploreDir.Format("%s%s", drive, dir);

				ShellExecute(win, "open", ExploreDir, "", "", SW_NORMAL);
			}
		return true;
	}
	else {
		if(Parent) Parent->MessageBox("File '" + Filename + "' could not be found.", NULL, MB_OK|MB_ICONERROR);
		return false;
	}

}
