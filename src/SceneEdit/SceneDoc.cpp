// SceneDoc.cpp : implementation of the CSceneDoc class
//

#include "stdafx.h"
#include "SceneEdit.h"
#include "SceneView.h"
#include "RootDlg.h"
#include <shlwapi.h>
#include <direct.h>
#include "dcgf_ad.h"

#include "SceneDoc.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#pragma comment(lib, "Shlwapi.lib")


#define PROJECT_ROOT_STRING "; $EDITOR_PROJECT_ROOT_DIR$ "


/////////////////////////////////////////////////////////////////////////////
// CSceneDoc

IMPLEMENT_DYNCREATE(CSceneDoc, CDocument)

BEGIN_MESSAGE_MAP(CSceneDoc, CDocument)
	//{{AFX_MSG_MAP(CSceneDoc)
		// NOTE - the ClassWizard will add and remove mapping macros here.
		//    DO NOT EDIT what you see in these blocks of generated code!
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CSceneDoc construction/destruction


//////////////////////////////////////////////////////////////////////////
CSceneDoc::CSceneDoc()
{
	// TODO: add one-time construction code here

}


//////////////////////////////////////////////////////////////////////////
CSceneDoc::~CSceneDoc()
{
}


//////////////////////////////////////////////////////////////////////////
BOOL CSceneDoc::OnNewDocument()
{
	if(!QueryProjectRoot()) return FALSE;	

	if (!CDocument::OnNewDocument())
		return FALSE;

	// TODO: add reinitialization code here
	// (SDI documents will reuse this document)

	return TRUE;
}



/////////////////////////////////////////////////////////////////////////////
// CSceneDoc serialization

void CSceneDoc::Serialize(CArchive& ar)
{
	if (ar.IsStoring())
	{
		// TODO: add storing code here
	}
	else
	{
		// TODO: add loading code here
	}
}

/////////////////////////////////////////////////////////////////////////////
// CSceneDoc diagnostics

#ifdef _DEBUG
void CSceneDoc::AssertValid() const
{
	CDocument::AssertValid();
}

void CSceneDoc::Dump(CDumpContext& dc) const
{
	CDocument::Dump(dc);
}
#endif //_DEBUG

/////////////////////////////////////////////////////////////////////////////
// CSceneDoc commands

BOOL CSceneDoc::OnOpenDocument(LPCTSTR lpszPathName) 
{
	if (!CDocument::OnOpenDocument(lpszPathName))
		return FALSE;
		
	CStdioFile f;
	CString line;
	bool RootFound = false;
	if(f.Open(lpszPathName, CFile::modeRead | CFile::typeText)){
		while(f.ReadString(line)){
			if(line.Find(PROJECT_ROOT_STRING)==0){
				CString root = line.Right(line.GetLength() - strlen(PROJECT_ROOT_STRING));
				root.TrimLeft();
				root.TrimRight();


				char drive[_MAX_DRIVE];
				char dir[_MAX_DIR];
				_splitpath(lpszPathName, drive, dir, NULL, NULL);
				
				char AbsPath[MAX_PATH];
				sprintf(AbsPath, "%s%s", drive, dir);
				chdir(AbsPath);
				_fullpath(AbsPath, LPCSTR(root), MAX_PATH);

				m_ProjectRoot = CString(AbsPath);
				if(m_ProjectRoot[m_ProjectRoot.GetLength()-1]!='\\') m_ProjectRoot+="\\";


				DWORD attr = GetFileAttributes(LPCSTR(AbsPath));
				if(attr == 0xFFFFFFFF || (attr & FILE_ATTRIBUTE_DIRECTORY) == 0){
					RootFound = false;
				}
				else{					
					RootFound = true;
				}

				break;
			}
		}
		f.Close();
	}

	if(RootFound){
		((CSceneEditApp*)AfxGetApp())->AddProjectRoot(m_ProjectRoot);
		return TRUE;
	}
	else return QueryProjectRoot(lpszPathName);
}


//////////////////////////////////////////////////////////////////////////
BOOL CSceneDoc::OnSaveDocument(LPCTSTR lpszPathName) 
{
	SetCurrentDirectory(m_ProjectRoot);

	POSITION pos = GetFirstViewPosition();
	CSceneView* View = (CSceneView*)GetNextView(pos);
	
	if(View){
		View->UpdateSelectionState();
		CBDynBuffer* buf = ::new CBDynBuffer(View->Game);
		buf->PutText("; generated by SceneEdit\n\n");
		WriteEditorData(lpszPathName, buf);
		View->Game->m_Scene->SaveAsText(buf, 0);


		FILE* f = fopen(lpszPathName, "wt");
		if(!f){
			delete buf;
			return FALSE;
		}
		
		fwrite(buf->m_Buffer, buf->GetSize(), 1, f);
		delete buf;
		fclose(f);
		SetModifiedFlag(FALSE);
		return TRUE;
	}
	else return FALSE;

	//return CDocument::OnSaveDocument(lpszPathName);
}



//////////////////////////////////////////////////////////////////////////
bool CSceneDoc::QueryProjectRoot(CString Filename)
{
	CFrameWnd* frame = ((CMDIFrameWnd*)AfxGetMainWnd())->GetActiveFrame();
		
	CRootDlg dlg;
	CSceneEditApp* app = (CSceneEditApp*)AfxGetApp();
	for(int i=0; i<app->m_ProjectRoots.GetSize(); i++){
		dlg.AddPrevFolder(app->m_ProjectRoots[i]);
	}
	
	dlg.m_Filename = Filename;
	dlg.m_SelectedFolder = m_ProjectRoot;
	
	if(dlg.DoModal()==IDOK){
		//MessageBox(NULL, dlg.m_SelectedFolder, "", MB_OK);
		if(frame) frame->ShowWindow(SW_SHOW);
		m_ProjectRoot = dlg.m_SelectedFolder;
		app->AddProjectRoot(dlg.m_SelectedFolder);
		return true;
	}
	else return false;
}


//////////////////////////////////////////////////////////////////////////
HRESULT CSceneDoc::WriteEditorData(LPCTSTR lpszPathName, CBDynBuffer *Buffer)
{
	char RelPath[MAX_PATH];
	//PathRelativePathTo(RelPath, LPCSTR(m_ProjectRoot), FILE_ATTRIBUTE_NORMAL, lpszPathName, FILE_ATTRIBUTE_NORMAL);
	if(!PathRelativePathTo(RelPath, lpszPathName, FILE_ATTRIBUTE_NORMAL, LPCSTR(m_ProjectRoot), FILE_ATTRIBUTE_NORMAL)){
		strcpy(RelPath, LPCSTR(m_ProjectRoot));
	}

	Buffer->PutTextIndent(0, "%s%s\n\n", PROJECT_ROOT_STRING, RelPath);
	return S_OK;
}
