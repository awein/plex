
#include "stdafx.h"
#include "GUIWindowPictures.h"
#include "Util.h"
#include "Picture.h"
#include "application.h"
#include "GUIThumbnailPanel.h"
#include "GUIPassword.h"
#include "FileSystem/ZipManager.h"
#include "GUIDialogContextMenu.h"
#include "GUIWindowFileManager.h"
#include "PlayListFactory.h"
#ifdef PRE_SKIN_VERSION_2_0_COMPATIBILITY
#include "SkinInfo.h"
#endif

#define CONTROL_BTNVIEWASICONS      2
#define CONTROL_BTNSORTBY           3
#define CONTROL_BTNSORTASC          4
#define CONTROL_LIST               50
#define CONTROL_THUMBS             51
#define CONTROL_LABELFILES         12

#define CONTROL_BTNSLIDESHOW   6
#define CONTROL_BTNSLIDESHOW_RECURSIVE   7
#define CONTROL_SHUFFLE      9


CGUIWindowPictures::CGUIWindowPictures(void)
    : CGUIMediaWindow(WINDOW_PICTURES, "MyPics.xml")
{
  m_thumbLoader.SetObserver(this);
}

CGUIWindowPictures::~CGUIWindowPictures(void)
{
}

bool CGUIWindowPictures::OnMessage(CGUIMessage& message)
{
  switch ( message.GetMessage() )
  {
  case GUI_MSG_DVDDRIVE_EJECTED_CD:
    {
      if ( !m_vecItems.IsVirtualDirectoryRoot() )
      {
        if ( m_vecItems.IsCDDA() || m_vecItems.IsDVD() || m_vecItems.IsISO9660() )
        {
          // Disc has changed and we are inside a DVD Drive share, get out of here :)
          m_vecItems.m_strPath.Empty();
          Update( m_vecItems.m_strPath );
        }
      }
      else
      {
        int iItem = m_viewControl.GetSelectedItem();
        Update( m_vecItems.m_strPath );
        m_viewControl.SetSelectedItem(iItem);
      }
    }
    break;
  case GUI_MSG_DVDDRIVE_CHANGED_CD:
    {
      if ( m_vecItems.IsVirtualDirectoryRoot() )
      {
        int iItem = m_viewControl.GetSelectedItem();
        Update( m_vecItems.m_strPath );
        m_viewControl.SetSelectedItem(iItem);
      }
    }
    break;
  case GUI_MSG_WINDOW_DEINIT:
    {
      if (m_thumbLoader.IsLoading())
        m_thumbLoader.StopThread();

      if (message.GetParam1() != WINDOW_SLIDESHOW)
      {
        m_ImageLib.Unload();
      }
    }
    break;

  case GUI_MSG_WINDOW_INIT:
    {
      int iLastControl = m_iLastControl;
      CGUIWindow::OnMessage(message);
      if (message.GetParam1() != WINDOW_SLIDESHOW)
      {
        m_ImageLib.Load();
      }

      CGUIThumbnailPanel* pControl=(CGUIThumbnailPanel*)GetControl(CONTROL_THUMBS);
      if (pControl)
        pControl->HideFileNameLabel(g_guiSettings.GetBool("Pictures.HideFilenamesInThumbPanel"));
      
      m_dlgProgress = (CGUIDialogProgress*)m_gWindowManager.GetWindow(WINDOW_DIALOG_PROGRESS);
            
      // check for a passed destination path
      CStdString strDestination = message.GetStringParam();
      if (!strDestination.IsEmpty())
      {
        message.SetStringParam("");
        CLog::Log(LOGINFO, "Attempting to quickpath to: %s", strDestination.c_str());
        m_vecPathHistory.clear();
      }
      // otherwise, is this the first time accessing this window?
      else if (m_vecItems.m_strPath == "?")
      {
        m_vecItems.m_strPath = strDestination = g_stSettings.m_szDefaultPictures;
        CLog::Log(LOGINFO, "Attempting to default to: %s", strDestination.c_str());
      }
      
      m_rootDir.SetMask(g_stSettings.m_szMyPicturesExtensions);
      m_rootDir.SetShares(g_settings.m_vecMyPictureShares);

      for (unsigned int i=0;i<m_rootDir.GetNumberOfShares();++i)
      {
        const CShare& share = m_rootDir[i];
        CURL url(share.strPath);
        if ((url.GetProtocol() != "zip") && (url.GetProtocol() != "rar"))
          continue;

        if (share.strEntryPoint.IsEmpty()) // do not unmount 'normal' rars/zips
          continue;
        
        if (url.GetProtocol() == "zip")
          g_ZipManager.release(share.strPath);
        
        strDestination = share.strEntryPoint;
        m_rootDir.RemoveShare(share.strPath);
      }
      
      // try to open the destination path
      if (!strDestination.IsEmpty())
      {
        // default parameters if the jump fails
        m_vecItems.m_strPath = "";

        bool bIsBookmarkName = false;
        int iIndex = CUtil::GetMatchingShare(strDestination, g_settings.m_vecMyPictureShares, bIsBookmarkName);
        if (iIndex > -1)
        {
          // set current directory to matching share
          if (bIsBookmarkName)
            m_vecItems.m_strPath = g_settings.m_vecMyPictureShares[iIndex].strPath;
          else
            m_vecItems.m_strPath = strDestination;
          CLog::Log(LOGINFO, "  Success! Opened destination path: %s", strDestination.c_str());
        }
        else
        {
          CLog::Log(LOGERROR, "  Failed! Destination parameter (%s) does not match a valid share!", strDestination.c_str());
        }
        SetHistoryForPath(m_vecItems.m_strPath);
      }

      Update(m_vecItems.m_strPath);

      if (iLastControl > -1)
      {
        SET_CONTROL_FOCUS(iLastControl, 0);
      }
      else
      {
        SET_CONTROL_FOCUS(m_dwDefaultFocusControlID, 0);
      }

      if (m_iSelectedItem >= 0)
      {
        m_viewControl.SetSelectedItem(m_iSelectedItem);
      }

      return true;
    }
    break;

  case GUI_MSG_CLICKED:
    {
      int iControl = message.GetSenderId();
      if (iControl == CONTROL_BTNSLIDESHOW) // Slide Show
      {
        OnSlideShow();
      }
      else if (iControl == CONTROL_BTNSLIDESHOW_RECURSIVE) // Recursive Slide Show
      {
        OnSlideShowRecursive();
      }
      else if (iControl == CONTROL_SHUFFLE)
      {
        g_guiSettings.ToggleBool("Slideshow.Shuffle");
        g_settings.Save();
      }
      else if (m_viewControl.HasControl(iControl))  // list/thumb control
      {
        int iItem = m_viewControl.GetSelectedItem();
        int iAction = message.GetParam1();

        // iItem is checked for validity inside these routines
        if (iAction == ACTION_CONTEXT_MENU || iAction == ACTION_MOUSE_RIGHT_CLICK)
        {
          OnPopupMenu(iItem);
        }
        else if (iAction == ACTION_DELETE_ITEM)
        {
          // is delete allowed?
          if (g_guiSettings.GetBool("Pictures.AllowFileDeletion"))
            OnDeleteItem(iItem);
          else
            return false;
        }
      }
    }
    break;
  }
  return CGUIMediaWindow::OnMessage(message);
}

void CGUIWindowPictures::FormatItemLabels()
{
  auto_ptr<CGUIViewState> pState(CGUIViewState::GetViewState(GetID(), m_vecItems));
  if (!pState.get()) return;
  for (int i = 0; i < m_vecItems.Size(); i++)
  {
    CFileItem* pItem = m_vecItems[i];
    if (pState->GetSortMethod() == SORT_METHOD_LABEL || pState->GetSortMethod() == SORT_METHOD_SIZE)
    {
      if (pItem->m_bIsFolder)
        pItem->SetLabel2("");
      else
        pItem->SetFileSizeLabel();
    }
    else
    {
      if (pItem->m_stTime.wYear)
      {
        CStdString strDateTime;
        CUtil::GetDate(pItem->m_stTime, strDateTime);
        pItem->SetLabel2(strDateTime);
      }
      else
        pItem->SetLabel2("");
    }
  }
}

void CGUIWindowPictures::UpdateButtons()
{
  CGUIMediaWindow::UpdateButtons();

  // Update the shuffle button
  if (g_guiSettings.GetBool("Slideshow.Shuffle"))
  {
    CGUIMessage msg2(GUI_MSG_SELECTED, GetID(), CONTROL_SHUFFLE, 0, 0, NULL);
    g_graphicsContext.SendMessage(msg2);
  }
  else
  {
    CGUIMessage msg2(GUI_MSG_DESELECTED, GetID(), CONTROL_SHUFFLE, 0, 0, NULL);
    g_graphicsContext.SendMessage(msg2);
  }

  // check we can slideshow or recursive slideshow
  int nFolders = m_vecItems.GetFolderCount();
  if (nFolders == m_vecItems.Size())
  {
    CONTROL_DISABLE(CONTROL_BTNSLIDESHOW);
  }
  else
  {
    CONTROL_ENABLE(CONTROL_BTNSLIDESHOW);
  }
  if (m_vecItems.Size() == 0)
  {
    CONTROL_DISABLE(CONTROL_BTNSLIDESHOW_RECURSIVE);
  }
  else
  {
    CONTROL_ENABLE(CONTROL_BTNSLIDESHOW_RECURSIVE);
  }
}

bool CGUIWindowPictures::Update(const CStdString &strDirectory)
{
  if (m_thumbLoader.IsLoading())
    m_thumbLoader.StopThread();

  if (!UpdateDir(strDirectory))
    return false;

  if (!m_vecItems.IsVirtualDirectoryRoot() && g_guiSettings.GetBool("Pictures.UseAutoSwitching"))
  {
    UpdateButtons();
  }

  m_thumbLoader.Load(m_vecItems);

  if ((m_vecPathHistory.size() == 0) || m_vecPathHistory.back() != strDirectory)
  {
    m_vecPathHistory.push_back(strDirectory);
  }

  // debug log
  CStdString strTemp;
  CLog::Log(LOGDEBUG,"Current m_vecPathHistory:");
  for (int i = 0; i < (int)m_vecPathHistory.size(); ++i)
  {
    strTemp.Format("%02i.[%s]", i, m_vecPathHistory[i]);
    CLog::Log(LOGDEBUG, "  %s", strTemp.c_str());
  }

  return true;
}

bool CGUIWindowPictures::UpdateDir(const CStdString &strDirectory)
{
  // get selected item
  int iItem = m_viewControl.GetSelectedItem();
  CStdString strSelectedItem = "";
  if (iItem >= 0 && iItem < (int)m_vecItems.Size())
  {
      CFileItem* pItem = m_vecItems[iItem];
    if (!pItem->IsParentFolder())
    {
      GetDirectoryHistoryString(pItem, strSelectedItem);
      m_history.Set(strSelectedItem, m_vecItems.m_strPath);
    }
  }

  CStdString strOldDirectory=m_vecItems.m_strPath;
  m_vecItems.m_strPath = strDirectory;

  CFileItemList items;
  if (!GetDirectory(m_vecItems.m_strPath, items))
  {
    m_vecItems.m_strPath = strOldDirectory;
    return false;
  }

  m_history.Set(strSelectedItem, strOldDirectory);

  ClearFileItems();

  m_vecItems.AppendPointer(items);
  m_vecItems.m_strPath = items.m_strPath;
  items.ClearKeepPointer();

  if (g_guiSettings.GetBool("FileLists.HideExtensions"))
    m_vecItems.RemoveExtensions();
  m_vecItems.FillInDefaultIcons();
  OnSort();
  UpdateButtons();

  strSelectedItem = m_history.Get(m_vecItems.m_strPath);

  for (int i = 0; i < (int)m_vecItems.Size(); ++i)
  {
    CFileItem* pItem = m_vecItems[i];
    CStdString strHistory;
    GetDirectoryHistoryString(pItem, strHistory);
    if (strHistory == strSelectedItem)
    {
      m_viewControl.SetSelectedItem(i);
      break;
    }
  }

  return true;
}

void CGUIWindowPictures::OnClick(int iItem)
{
  if ( iItem < 0 || iItem >= (int)m_vecItems.Size() ) return ;
  CFileItem* pItem = m_vecItems[iItem];
  CStdString strPath = pItem->m_strPath;
  if (pItem->m_bIsFolder)
  {
    if ( !g_passwordManager.IsItemUnlocked( pItem, "pictures" ) )
      return ;

    if (pItem->IsParentFolder())
    {
      // go back a directory
      GoParentFolder();

      // GoParentFolder() calls Update(), so just return
      return;
    }

    m_iSelectedItem = -1;
    if ( pItem->m_bIsShareOrDrive )
    {
      if ( !HaveDiscOrConnection( pItem->m_strPath, pItem->m_iDriveType ) )
        return ;
    }
    if (!Update(strPath))
      ShowShareErrorMessage(pItem);
  }
  else if (pItem->IsZIP() && g_guiSettings.GetBool("Pictures.HandleArchives")) // mount zip archive
  {
    CShare shareZip;
    shareZip.strPath.Format("zip://Z:\\,%i,,%s,\\",1, pItem->m_strPath.c_str() );
    shareZip.strEntryPoint.Empty();
    m_rootDir.AddShare(shareZip);
    m_iSelectedItem = -1;
    Update(shareZip.strPath);
  }
  else if (pItem->IsRAR() && g_guiSettings.GetBool("Pictures.HandleArchives")) // mount rar archive
  {
    CShare shareRar;
    shareRar.strPath.Format("rar://Z:\\,%i,,%s,\\",EXFILE_AUTODELETE, pItem->m_strPath.c_str() );
    shareRar.strEntryPoint.Empty();
    m_rootDir.AddShare(shareRar);
    m_iSelectedItem = -1;
    Update(shareRar.strPath);
  }
  else if (pItem->IsCBZ()) //mount'n'show'n'unmount
  {
    CUtil::GetDirectory(pItem->m_strPath,strPath);
    CShare shareZip;
    shareZip.strPath.Format("zip://Z:\\filesrar\\,%i,,%s,\\",1, pItem->m_strPath.c_str() );
    shareZip.strEntryPoint = strPath;
    m_rootDir.AddShare(shareZip);
    Update(shareZip.strPath);
    if (m_vecItems.Size() > 0)
    {
      CStdString strEmpty; strEmpty.Empty();
      OnShowPictureRecursive(strEmpty);
    }
    else
    {
      CLog::Log(LOGERROR,"No pictures found in cbz file!");
      m_rootDir.RemoveShare(shareZip.strPath);
      Update(strPath);
    }
    m_iSelectedItem = iItem;
  }
  else if (pItem->IsCBR()) // mount'n'show'n'unmount
  {
    CUtil::GetDirectory(pItem->m_strPath,strPath);
    CShare shareRar;
    shareRar.strPath.Format("rar://Z:\\,%i,,%s,\\",EXFILE_AUTODELETE,pItem->m_strPath.c_str() );
    shareRar.strEntryPoint = strPath;
    m_rootDir.AddShare(shareRar);
    m_iSelectedItem = -1;
    Update(shareRar.strPath);
    if (m_vecItems.Size() > 0)
    {
      CStdString strEmpty; strEmpty.Empty();
      OnShowPictureRecursive(strEmpty);
    }
    else
    {
      CLog::Log(LOGERROR,"No pictures found in cbr file!");
      m_rootDir.RemoveShare(shareRar.strPath);
      Update(strPath);
    }
    m_iSelectedItem = iItem;
  }
  else if (pItem->IsPlayList())
  {
    LoadPlayList(strPath);
  }
  else
  {
    // show picture
    m_iSelectedItem = m_viewControl.GetSelectedItem();
    OnShowPicture(strPath);
  }
}

void CGUIWindowPictures::OnShowPicture(const CStdString& strPicture)
{
  CGUIWindowSlideShow *pSlideShow = (CGUIWindowSlideShow *)m_gWindowManager.GetWindow(WINDOW_SLIDESHOW);
  if (!pSlideShow)
    return ;
  if (g_application.IsPlayingVideo())
    g_application.StopPlaying();

  pSlideShow->Reset();
  for (int i = 0; i < (int)m_vecItems.Size();++i)
  {
    CFileItem* pItem = m_vecItems[i];
    if (!pItem->m_bIsFolder && !(CUtil::IsRAR(pItem->m_strPath) || CUtil::IsZIP(pItem->m_strPath)))
    {
      pSlideShow->Add(pItem->m_strPath);
    }
  }
  pSlideShow->Select(strPicture);
  m_gWindowManager.ActivateWindow(WINDOW_SLIDESHOW);
}

void CGUIWindowPictures::OnShowPictureRecursive(const CStdString& strPicture)
{
  CGUIWindowSlideShow *pSlideShow = (CGUIWindowSlideShow *)m_gWindowManager.GetWindow(WINDOW_SLIDESHOW);
  if (!pSlideShow)
    return ;
  if (g_application.IsPlayingVideo())
    g_application.StopPlaying();

  pSlideShow->Reset();
  for (int i = 0; i < (int)m_vecItems.Size();++i)
  {
    CFileItem* pItem = m_vecItems[i];
    if (pItem->m_bIsFolder)
      AddDir(pSlideShow, pItem->m_strPath);
    else if (!(CUtil::IsRAR(pItem->m_strPath) || CUtil::IsZIP(pItem->m_strPath)))
      pSlideShow->Add(pItem->m_strPath);
  }
  if (!strPicture.IsEmpty())
    pSlideShow->Select(strPicture);
  m_gWindowManager.ActivateWindow(WINDOW_SLIDESHOW);
}

void CGUIWindowPictures::AddDir(CGUIWindowSlideShow *pSlideShow, const CStdString& strPath)
{
  if (!pSlideShow) return ;
  CFileItemList items;
  m_rootDir.GetDirectory(strPath, items);
  SortItems(items);

  for (int i = 0; i < (int)items.Size();++i)
  {
    CFileItem* pItem = items[i];
    if (pItem->m_bIsFolder)
      AddDir(pSlideShow, pItem->m_strPath);
    else if (!(CUtil::IsRAR(pItem->m_strPath) || CUtil::IsZIP(pItem->m_strPath)))
      pSlideShow->Add(pItem->m_strPath);
  }
}

void CGUIWindowPictures::OnSlideShowRecursive(const CStdString &strPicture)
{
  CGUIWindowSlideShow *pSlideShow = (CGUIWindowSlideShow *)m_gWindowManager.GetWindow(WINDOW_SLIDESHOW);
  if (!pSlideShow)
    return ;

  if (g_application.IsPlayingVideo())
    g_application.StopPlaying();

  pSlideShow->Reset();
  AddDir(pSlideShow, m_vecItems.m_strPath);
  pSlideShow->StartSlideShow();
  if (!strPicture.IsEmpty())
    pSlideShow->Select(strPicture);
  if (pSlideShow->NumSlides())
    m_gWindowManager.ActivateWindow(WINDOW_SLIDESHOW);
}

void CGUIWindowPictures::OnSlideShowRecursive()
{
  CStdString strEmpty = "";
  OnSlideShowRecursive(strEmpty);
}

void CGUIWindowPictures::OnSlideShow()
{
  CStdString strEmpty = "";
  OnSlideShow(strEmpty);
}

void CGUIWindowPictures::OnSlideShow(const CStdString &strPicture)
{
  CGUIWindowSlideShow *pSlideShow = (CGUIWindowSlideShow *)m_gWindowManager.GetWindow(WINDOW_SLIDESHOW);
  if (!pSlideShow)
    return ;
  if (g_application.IsPlayingVideo())
    g_application.StopPlaying();

  pSlideShow->Reset();
  for (int i = 0; i < (int)m_vecItems.Size();++i)
  {
    CFileItem* pItem = m_vecItems[i];
    if (!pItem->m_bIsFolder && !(CUtil::IsRAR(pItem->m_strPath) || CUtil::IsZIP(pItem->m_strPath)))
    {
      pSlideShow->Add(pItem->m_strPath);
    }
  }
  pSlideShow->StartSlideShow();
  if (!strPicture.IsEmpty())
    pSlideShow->Select(strPicture);
  if (pSlideShow->NumSlides())
    m_gWindowManager.ActivateWindow(WINDOW_SLIDESHOW);
}

void CGUIWindowPictures::OnRegenerateThumbs()
{
  if (m_thumbLoader.IsLoading()) return;
  m_thumbLoader.SetRegenerateThumbs(true);
  m_thumbLoader.Load(m_vecItems);
}

void CGUIWindowPictures::GoParentFolder()
{
  // remove current directory if its on the stack
  if (m_vecPathHistory.size() > 0)
  {
    if (m_vecPathHistory.back() == m_vecItems.m_strPath)
      m_vecPathHistory.pop_back();
  }

  // if vector is not empty, pop parent
  // if vector is empty, parent is bookmark listing
  CStdString strParent = "";
  if (m_vecPathHistory.size() > 0)
  {
    strParent = m_vecPathHistory.back();
    m_vecPathHistory.pop_back();
  }
  CLog::Log(LOGDEBUG,"CGUIWindowPictures::GoParentFolder(), strParent = [%s]", strParent.c_str());

  CURL url(m_vecItems.m_strPath);
  if ((url.GetProtocol() == "rar") || (url.GetProtocol() == "zip")) 
  {
    // check for step-below, if, unmount rar
    if (url.GetFileName().IsEmpty())
    {
      if (url.GetProtocol() == "zip")
        g_ZipManager.release(m_vecItems.m_strPath); // release resources
      m_rootDir.RemoveShare(m_vecItems.m_strPath);
      CStdString strPath;
      CUtil::GetDirectory(url.GetHostName(),strPath);
      Update(strPath);
      return;
    }
  }
  
  CStdString strOldPath = m_vecItems.m_strPath;
  Update(strParent);

  if (!g_guiSettings.GetBool("LookAndFeel.FullDirectoryHistory"))
    m_history.Remove(strOldPath); //Delete current path
}

/// \brief Build a directory history string
/// \param pItem Item to build the history string from
/// \param strHistoryString History string build as return value
void CGUIWindowPictures::GetDirectoryHistoryString(const CFileItem* pItem, CStdString& strHistoryString)
{
  if (pItem->m_bIsShareOrDrive)
  {
    // We are in the virual directory

    // History string of the DVD drive
    // must be handel separately
    if (pItem->m_iDriveType == SHARE_TYPE_DVD)
    {
      // Remove disc label from item label
      // and use as history string, m_strPath
      // can change for new discs
      CStdString strLabel = pItem->GetLabel();
      int nPosOpen = strLabel.Find('(');
      int nPosClose = strLabel.ReverseFind(')');
      if (nPosOpen > -1 && nPosClose > -1 && nPosClose > nPosOpen)
      {
        strLabel.Delete(nPosOpen + 1, (nPosClose) - (nPosOpen + 1));
        strHistoryString = strLabel;
      }
      else
        strHistoryString = strLabel;
    }
    else
    {
      // Other items in virual directory
      CStdString strPath = pItem->m_strPath;
      while (CUtil::HasSlashAtEnd(strPath))
        strPath.Delete(strPath.size() - 1);

      strHistoryString = pItem->GetLabel() + strPath;
    }
  }
  else
  {
    // Normal directory items
    strHistoryString = pItem->m_strPath;

    if (CUtil::HasSlashAtEnd(strHistoryString))
      strHistoryString.Delete(strHistoryString.size() - 1);
  }
}

void CGUIWindowPictures::SetHistoryForPath(const CStdString& strDirectory)
{
  if (!strDirectory.IsEmpty())
  {
    // Build the directory history for default path
    CStdString strPath, strParentPath;
    strPath = strDirectory;
    CFileItemList items;
    GetDirectory("", items);

    while (CUtil::GetParentPath(strPath, strParentPath))
    {
      bool bSet = false;
      for (int i = 0; i < items.Size(); ++i)
      {
        CFileItem* pItem = items[i];
        while (CUtil::HasSlashAtEnd(pItem->m_strPath))
          pItem->m_strPath.Delete(pItem->m_strPath.size() - 1);
        if (pItem->m_strPath == strPath)
        {
          CStdString strHistory;
          GetDirectoryHistoryString(pItem, strHistory);
          m_history.Set(strHistory, "");
          return ;
        }
      }

      m_history.Set(strPath, strParentPath);
      strPath = strParentPath;
      while (CUtil::HasSlashAtEnd(strPath))
        strPath.Delete(strPath.size() - 1);
    }
    items.Clear();
  }
}

bool CGUIWindowPictures::GetDirectory(const CStdString &strDirectory, CFileItemList &items)
{
  if (items.Size())
  {
    // cleanup items
    items.Clear();
  }

  CStdString strParentPath = "";
  if (m_vecPathHistory.size() > 0)
    strParentPath = m_vecPathHistory.back();

  CLog::Log(LOGDEBUG,"CGUIWindowPicutres::GetDirectory (%s)", strDirectory.c_str());
  CLog::Log(LOGDEBUG,"  ParentPath = [%s]", strParentPath.c_str());

  if (!g_guiSettings.GetBool("Pictures.HideParentDirItems"))
  {
    CFileItem *pItem = new CFileItem("..");
    pItem->m_strPath = strParentPath;
    pItem->m_bIsFolder = true;
    pItem->m_bIsShareOrDrive = false;
    items.Add(pItem);
  }
  m_strParentPath = strParentPath;

  CLog::Log(LOGDEBUG,"Fetching directory (%s)", strDirectory.c_str());
  if (!m_rootDir.GetDirectory(strDirectory, items))
  {
    CLog::Log(LOGERROR,"GetDirectory(%s) failed", strDirectory.c_str());
    return false;
  }

  return true;
}

void CGUIWindowPictures::OnPopupMenu(int iItem)
{
  // calculate our position
  int iPosX = 200, iPosY = 100;
  const CGUIControl *pList = GetControl(CONTROL_LIST);
  if (pList)
  {
    iPosX = pList->GetXPosition() + pList->GetWidth() / 2;
    iPosY = pList->GetYPosition() + pList->GetHeight() / 2;
  }
  if ( m_vecItems.IsVirtualDirectoryRoot() )
  {
    if (iItem < 0)
    { // we should add the option here of adding shares
      return ;
    }
    // mark the item
    m_vecItems[iItem]->Select(true);
    bool bMaxRetryExceeded = false;
    if (g_stSettings.m_iMasterLockMaxRetry != 0)
      bMaxRetryExceeded = !(m_vecItems[iItem]->m_iBadPwdCount < g_stSettings.m_iMasterLockMaxRetry);

    // and do the popup menu
    if (CGUIDialogContextMenu::BookmarksMenu("pictures", m_vecItems[iItem]->GetLabel(), m_vecItems[iItem]->m_strPath, m_vecItems[iItem]->m_iLockMode, bMaxRetryExceeded, iPosX, iPosY))
    {
      m_rootDir.SetShares(g_settings.m_vecMyPictureShares);
      Update(m_vecItems.m_strPath);
      return ;
    }
  }
  else if (iItem >= 0)
  {
    // mark the item
    m_vecItems[iItem]->Select(true);
    // popup the context menu
    CGUIDialogContextMenu *pMenu = (CGUIDialogContextMenu *)m_gWindowManager.GetWindow(WINDOW_DIALOG_CONTEXT_MENU);
    if (!pMenu) return ;
    // load our menu
    pMenu->Initialize();
    // add the needed buttons
    
    int btn_Thumbs       = 0; // Create Thumbnails
    int btn_SlideShow    = 0; // View Slideshow
    int btn_RecSlideShow = 0; // Recursive Slideshow

    // this could be done like the delete button too
    if (m_vecItems.GetFileCount() != 0) 
      btn_SlideShow = pMenu->AddButton(13317);      // View Slideshow
    
    btn_RecSlideShow = pMenu->AddButton(13318);     // Recursive Slideshow
    
    if (!m_thumbLoader.IsLoading()) 
      btn_Thumbs = pMenu->AddButton(13315);         // Create Thumbnails
    
    int btn_Delete = 0, btn_Rename = 0;             // Delete and Rename
    if (g_guiSettings.GetBool("Pictures.AllowFileDeletion"))
    {
      btn_Delete = pMenu->AddButton(117);           // Delete
      btn_Rename = pMenu->AddButton(118);           // Rename
    }
    
    int btn_Settings = pMenu->AddButton(5);         // Settings

    // position it correctly
    pMenu->SetPosition(iPosX - pMenu->GetWidth() / 2, iPosY - pMenu->GetHeight() / 2);
    pMenu->DoModal(GetID());

    int btnid = pMenu->GetButton();

    if (btnid>0)
    {
      if (btnid == btn_SlideShow)
      {
        OnSlideShow(m_vecItems[iItem]->m_strPath);
        return;
      }
      else if (btnid == btn_RecSlideShow)
      {
        OnSlideShowRecursive(m_vecItems[iItem]->m_strPath);
        return;
      }
      else if (btnid == btn_Thumbs)
      {
        OnRegenerateThumbs();
      }
      else if (btnid == btn_Delete)
      {
        OnDeleteItem(iItem);
      }
      //Rename
      else if (btnid == btn_Rename)
      {
        OnRenameItem(iItem);
      }
      else if (btnid == btn_Settings)
      {
        //MasterPassword
        int iLockSettings = g_guiSettings.GetInt("Masterlock.LockSettingsFilemanager");
        if (iLockSettings == 1 || iLockSettings == 3) 
        {
          if (g_passwordManager.IsMasterLockLocked(true))
            m_gWindowManager.ActivateWindow(WINDOW_SETTINGS_MYPICTURES);
        }
        else m_gWindowManager.ActivateWindow(WINDOW_SETTINGS_MYPICTURES); 
        return;
      }
    }
    m_vecItems[iItem]->Select(false);
  }
}

void CGUIWindowPictures::OnWindowLoaded()
{
#ifdef PRE_SKIN_VERSION_2_0_COMPATIBILITY
  if (g_SkinInfo.GetVersion() < 1.8)
  {
    ChangeControlID(10, CONTROL_LIST, CGUIControl::GUICONTROL_LIST);
    ChangeControlID(11, CONTROL_THUMBS, CGUIControl::GUICONTROL_THUMBNAIL);
  }
#endif
  CGUIMediaWindow::OnWindowLoaded();
}

void CGUIWindowPictures::OnItemLoaded(CFileItem *pItem)
{
  if (pItem->m_bIsFolder && !pItem->m_bIsShareOrDrive && !pItem->HasThumbnail() && !pItem->IsParentFolder())
  { // generate the thumb folder if necessary
    // we load the directory, grab 4 random thumb files (if available) and then generate
    // the thumb.
    if (pItem->IsRemote() && !pItem->IsOnDVD() && !g_guiSettings.GetBool("VideoFiles.FindRemoteThumbs")) return;

    CFileItemList items;
    CDirectory::GetDirectory(pItem->m_strPath, items, g_stSettings.m_szMyPicturesExtensions, false, false);

    // create the folder thumb by choosing 4 random thumbs within the folder and putting
    // them into one thumb.
    // count the number of images
    int numFiles = 0;
    for (int i=0; i < items.Size(); i++)
      if (items[i]->IsPicture() && !items[i]->IsZIP() && !items[i]->IsRAR())
        numFiles++;
    if (!numFiles) return;

    srand(timeGetTime());
    int thumbs[4];
    if (numFiles > 4)
    { // choose 4 random thumbs
      int i = 0;
      while (i < 4)
      {
        int thumbnum = rand() % numFiles;
        bool bFoundNew = true;
        for (int j = 0; j < i; j++)
        {
          if (thumbnum == thumbs[j])
          {
            bFoundNew = false;
          }
        }
        if (bFoundNew)
          thumbs[i++] = thumbnum;
      }
    }
    else
    {
      for (int i = 0; i < numFiles; i++)
        thumbs[i] = i;
      for (int i = numFiles; i < 4; i++)
        thumbs[i] = -1;
    }
    // ok, now we've got the files to get the thumbs from, lets create it...
    // we basically load the 4 thumbs, resample to 62x62 pixels, and add them
    CStdString strFiles[4];
    for (int thumb = 0; thumb < 4; thumb++)
    {
      if (thumbs[thumb] >= 0)
      {
        int files = 0;
        for (int i = 0; i < items.Size(); i++)
        {
          if (items[i]->IsPicture() && !items[i]->IsZIP() && !items[i]->IsRAR())
          {
            if (thumbs[thumb] == files)
              strFiles[thumb] = items[i]->m_strPath;
            files++;
          }
        }
      }
      else
        strFiles[thumb] = "";
    }
    CPicture pic;
    pic.CreateFolderThumb(pItem->m_strPath, strFiles);
    // refill in the icon to get it to update
    g_graphicsContext.Lock();
    pItem->FreeIcons();
    pItem->SetThumb();
    pItem->FillInDefaultIcon();
    g_graphicsContext.Unlock();
  }
}


void CGUIWindowPictures::OnDeleteItem(int iItem)
{
  if ( iItem < 0 || iItem >= m_vecItems.Size()) return;
  if (!CGUIWindowFileManager::DeleteItem(m_vecItems[iItem]))
    return;
  Update(m_vecItems.m_strPath);
  m_viewControl.SetSelectedItem(iItem);
}

void CGUIWindowPictures::OnRenameItem(int iItem)
{
  if ( iItem < 0 || iItem >= m_vecItems.Size()) return;
  if (!CGUIWindowFileManager::RenameFile(m_vecItems[iItem]->m_strPath))
    return;
  Update(m_vecItems.m_strPath);
  m_viewControl.SetSelectedItem(iItem);
}

void CGUIWindowPictures::ShowShareErrorMessage(CFileItem* pItem)
{
  if (pItem->m_bIsShareOrDrive)
  {
    int idMessageText=0;
    CURL url(pItem->m_strPath);
    const CStdString& strHostName=url.GetHostName();

    if (pItem->m_iDriveType!=SHARE_TYPE_REMOTE) //  Local shares incl. dvd drive
      idMessageText=15300;
    else if (url.GetProtocol()=="xbms" && strHostName.IsEmpty()) //  xbms server discover
      idMessageText=15302;
    else if (url.GetProtocol()=="smb" && strHostName.IsEmpty()) //  smb workgroup
      idMessageText=15303;
    else  //  All other remote shares
      idMessageText=15301;

    CGUIDialogOK::ShowAndGetInput(220, idMessageText, 0, 0);
  }
}

void CGUIWindowPictures::LoadPlayList(const CStdString& strPlayList)
{
  CLog::Log(LOGDEBUG,"CGUIWindowPictures::LoadPlayList()... converting playlist into slideshow: %s", strPlayList.c_str());
  CPlayListFactory factory;
  auto_ptr<CPlayList> pPlayList (factory.Create(strPlayList));
  if ( NULL != pPlayList.get())
  {
    if (!pPlayList->Load(strPlayList))
    {
      CGUIDialogOK::ShowAndGetInput(6, 0, 477, 0);
      return ; //hmmm unable to load playlist?
    }
  }

  CPlayList playlist = *pPlayList;
  if (playlist.size() > 0)
  {
    // set up slideshow
    CGUIWindowSlideShow *pSlideShow = (CGUIWindowSlideShow *)m_gWindowManager.GetWindow(WINDOW_SLIDESHOW);
    if (!pSlideShow)
      return;
    if (g_application.IsPlayingVideo())
      g_application.StopPlaying();

    // convert playlist items into slideshow items
    pSlideShow->Reset();
    for (int i = 0; i < (int)playlist.size(); ++i)
    {
      CFileItem *pItem = new CFileItem(playlist[i].m_strPath, false);
      //CLog::Log(LOGDEBUG,"-- playlist item: %s", pItem->m_strPath.c_str());
      if (pItem->IsPicture() && !(pItem->IsZIP() || pItem->IsRAR() || pItem->IsCBZ() || pItem->IsCBR()))
        pSlideShow->Add(pItem->m_strPath);
    }

    // start slideshow if there are items
    pSlideShow->StartSlideShow();
    if (pSlideShow->NumSlides())
      m_gWindowManager.ActivateWindow(WINDOW_SLIDESHOW);
  }
}