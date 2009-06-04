#include "Header.h"
#include <Tlhelp32.h>

// WARNING("не появляются табы во второй консоли");
WARNING("На предыдущей строке символы под курсором прыгают влево");
WARNING("При быстром наборе текста курсор часто 'замерзает' на одной из букв, но продолжает двигаться дальше");

WARNING("Глюк с частичной отрисовкой экрана часто появляется при Alt-F7, Enter. Особенно, если файла нет");
// Иногда не отрисовывается диалог поиска полностью - только бежит текущая сканируемая директория.
// Иногда диалог отрисовался, но часть до текста "..." отсутствует

//PRAGMA_ERROR("При попытке компиляции F:\\VCProject\\FarPlugin\\PPCReg\\compile.cmd - Enter в консоль не прошел");

TODO("CES_CONMANACTIVE deprecated");

WARNING("Может быть хватит ServerThread? А StartProcessThread зарезервировать только для запуска процесса, и сразу из него выйти");

WARNING("В каждой VCon создать буфер BYTE[256] для хранения распознанных клавиш (Ctrl,...,Up,PgDn,Add,и пр.");
WARNING("Нераспознанные можно помещать в буфер {VKEY,wchar_t=0}, в котором заполнять последний wchar_t по WM_CHAR/WM_SYSCHAR");
WARNING("При WM_(SYS)CHAR помещать wchar_t в начало, в первый незанятый VKEY");
WARNING("При нераспознном WM_KEYUP - брать(и убрать) wchar_t из этого буфера, послав в консоль UP");
TODO("А периодически - проводить процерку isKeyDown, и чистить буфер");
WARNING("при переключении на другую консоль (да наверное и в процессе просто нажатий - модификатор может быть изменен в самой программе) требуется проверка caps, scroll, num");
WARNING("а перед пересылкой символа/клавиши проверять нажат ли на клавиатуре Ctrl/Shift/Alt");

WARNING("Если отображается меню - панели недоступны! D&D невозможен, табы тоже");
/*
    Left    Files
║n        ╔════════
║aut      ║  View
*/

WARNING("Часто после разблокирования компьютера размер консоли изменяется (OK), но обновленное содержимое консоли не приходит в GUI - там остется обрезанная верхняя и нижняя строка");


//Курсор, его положение, размер консоли, измененный текст, и пр...

#define VCURSORWIDTH 2
#define HCURSORHEIGHT 2

#define Assert(V) if ((V)==FALSE) { TCHAR szAMsg[MAX_PATH*2]; wsprintf(szAMsg, _T("Assertion (%s) at\n%s:%i"), _T(#V), _T(__FILE__), __LINE__); Box(szAMsg); }

CVirtualConsole* CVirtualConsole::Create(bool abDetached)
{
    CVirtualConsole* pCon = new CVirtualConsole();
    
	if (!pCon->mp_RCon->PreCreate(abDetached)) {
		delete pCon;
		return NULL;
	}

    return pCon;
}

CVirtualConsole::CVirtualConsole(/*HANDLE hConsoleOutput*/)
{
	mp_RCon = new CRealConsole(this);

    SIZE_T nMinHeapSize = (1000 + (200 * 90 * 2) * 6 + MAX_PATH*2)*2 + 210*sizeof(*TextParts);
    mh_Heap = HeapCreate(HEAP_GENERATE_EXCEPTIONS, nMinHeapSize, 0);
   
	cinf.dwSize = 15; cinf.bVisible = TRUE;
	ZeroStruct(winSize); ZeroStruct(coord); ZeroStruct(select1); ZeroStruct(select2);
	TextLen = 0;
  
    InitializeCriticalSection(&csDC); ncsTDC = 0;
    InitializeCriticalSection(&csCON); ncsTCON = 0;

    mr_LeftPanel = mr_RightPanel = MakeRect(-1,-1);


#ifdef _DEBUG
    mn_BackColorIdx = 2;
#else
    mn_BackColorIdx = 0;
#endif
    memset(&Cursor, 0, sizeof(Cursor));
    Cursor.nBlinkTime = GetCaretBlinkTime();

    TextWidth = TextHeight = Width = Height = nMaxTextWidth = nMaxTextHeight = 0;
    hDC = NULL; hBitmap = NULL;
    hSelectedFont = NULL; hOldFont = NULL;
    ConChar = NULL; ConAttr = NULL; ConCharX = NULL; tmpOem = NULL; TextParts = NULL;
    memset(&SelectionInfo, 0, sizeof(SelectionInfo));
    mb_IsForceUpdate = false;
    hBrush0 = NULL; hSelectedBrush = NULL; hOldBrush = NULL;
    isEditor = false;
    memset(&csbi, 0, sizeof(csbi)); mdw_LastError = 0;

    nSpaceCount = 1000;
    Spaces = (TCHAR*)Alloc(nSpaceCount,sizeof(TCHAR));
    for (UINT i=0; i<nSpaceCount; i++) Spaces[i]=L' ';

    hOldBrush = NULL;
    hOldFont = NULL;
    
    if (gSet.wndWidth)
        TextWidth = gSet.wndWidth;
    if (gSet.wndHeight)
        TextHeight = gSet.wndHeight;
    

    if (gSet.isShowBgImage)
        gSet.LoadImageFrom(gSet.sBgImage);


    // InitDC звать бессмысленно - консоль еще не создана
    hDC = NULL;
    hBitmap = NULL;
    ConChar = NULL;
    ConAttr = NULL;
    ConCharX = NULL;
    tmpOem = NULL;
    TextParts = NULL;
}

CVirtualConsole::~CVirtualConsole()
{
    if (mp_RCon)
		mp_RCon->StopThread();
    
    MCHKHEAP
    if (hDC)
        { DeleteDC(hDC); hDC = NULL; }
    if (hBitmap)
        { DeleteObject(hBitmap); hBitmap = NULL; }
    if (ConChar)
        { Free(ConChar); ConChar = NULL; }
    if (ConAttr)
        { Free(ConAttr); ConAttr = NULL; }
    if (ConCharX)
        { Free(ConCharX); ConCharX = NULL; }
    if (tmpOem)
        { Free(tmpOem); tmpOem = NULL; }
    if (TextParts)
        { Free(TextParts); TextParts = NULL; }
    if (Spaces) 
        { Free(Spaces); Spaces = NULL; nSpaceCount = 0; }

    // Куча больше не нужна
    if (mh_Heap) {
        HeapDestroy(mh_Heap);
        mh_Heap = NULL;
    }

 
    
    DeleteCriticalSection(&csDC);
    DeleteCriticalSection(&csCON);

	if (mp_RCon) {
		delete mp_RCon;
		mp_RCon = NULL;
	}
}

#ifdef _DEBUG
#define HEAPVAL HeapValidate(mh_Heap, 0, NULL);
#else
#define HEAPVAL 
#endif


// InitDC вызывается только при критических изменениях (размеры, шрифт, и т.п.) когда нужно пересоздать DC и Bitmap
bool CVirtualConsole::InitDC(bool abNoDc, bool abNoWndResize)
{
    CSection SCON(NULL, NULL); //&csCON, &ncsTCON);
    BOOL lbNeedCreateBuffers = FALSE;

    // Буфер пересоздаем только если требуется его увеличение
    if (!ConChar || !ConAttr || !ConCharX || !tmpOem || !TextParts ||
        (nMaxTextWidth * nMaxTextHeight) < (mp_RCon->TextWidth() * mp_RCon->TextHeight()) ||
        (nMaxTextWidth < mp_RCon->TextWidth()) // а это нужно для TextParts & tmpOem
        )
        lbNeedCreateBuffers = TRUE;

    if (!mp_RCon->TextWidth() || !mp_RCon->TextHeight()) {
        Assert(mp_RCon->TextWidth() && mp_RCon->TextHeight());
        return false;
    }


    if (lbNeedCreateBuffers) {
        SCON.Enter(&csCON, &ncsTCON);

        HEAPVAL
        if (ConChar)
            { Free(ConChar); ConChar = NULL; }
        if (ConAttr)
            { Free(ConAttr); ConAttr = NULL; }
        if (ConCharX)
            { Free(ConCharX); ConCharX = NULL; }
        if (tmpOem)
            { Free(tmpOem); tmpOem = NULL; }
        if (TextParts)
            { Free(TextParts); TextParts = NULL; }
    }

    #ifdef _DEBUG
    HeapValidate(mh_Heap, 0, NULL);
    #endif

    //CONSOLE_SCREEN_BUFFER_INFO csbi;
    //if (!GetConsoleScreenBufferInfo())         return false;

    mb_IsForceUpdate = true;
    //TextWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    //TextHeight = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    TextWidth = mp_RCon->TextWidth();
    TextHeight = mp_RCon->TextHeight();

#ifdef _DEBUG
    _ASSERT(TextHeight >= 5);
#endif


    //if ((int)TextWidth < csbi.dwSize.X)
    //    TextWidth = csbi.dwSize.X;

    //MCHKHEAP
    if (lbNeedCreateBuffers) {
        if (nMaxTextWidth < TextWidth)
            nMaxTextWidth = TextWidth;
        if (nMaxTextHeight < TextHeight)
            nMaxTextHeight = TextHeight;

        HEAPVAL
        ConChar = (TCHAR*)Alloc((nMaxTextWidth * nMaxTextHeight * 2), sizeof(*ConChar));
        ConAttr = (WORD*)Alloc((nMaxTextWidth * nMaxTextHeight * 2), sizeof(*ConAttr));
        ConCharX = (DWORD*)Alloc((nMaxTextWidth * nMaxTextHeight), sizeof(*ConCharX));
        tmpOem = (char*)Alloc((nMaxTextWidth + 5), sizeof(*tmpOem));
        TextParts = (struct _TextParts*)Alloc((nMaxTextWidth + 2), sizeof(*TextParts));
        HEAPVAL
    }
    //MCHKHEAP
    if (!ConChar || !ConAttr || !ConCharX || !tmpOem || !TextParts)
        return false;

    if (!lbNeedCreateBuffers) {
        HEAPVAL
        ZeroMemory(ConChar, (TextWidth * TextHeight * 2)*sizeof(*ConChar));
        HEAPVAL
        ZeroMemory(ConAttr, (TextWidth * TextHeight * 2)*sizeof(*ConAttr));
        HEAPVAL
        ZeroMemory(ConCharX, (TextWidth * TextHeight)*sizeof(*ConCharX));
        HEAPVAL
        ZeroMemory(tmpOem, (TextWidth + 5)*sizeof(*tmpOem));
        HEAPVAL
        ZeroMemory(TextParts, (TextWidth + 2)*sizeof(*TextParts));
        HEAPVAL
    }

    SCON.Leave();

    HEAPVAL

    SelectionInfo.dwFlags = 0;

    hSelectedFont = NULL;

    // Это может быть, если отключена буферизация (debug) и вывод идет сразу на экран
    if (!abNoDc)
    {
        CSection SDC(&csDC, &ncsTDC);

        if (hDC)
        { DeleteDC(hDC); hDC = NULL; }
        if (hBitmap)
        { DeleteObject(hBitmap); hBitmap = NULL; }

        const HDC hScreenDC = GetDC(0);
        if (hDC = CreateCompatibleDC(hScreenDC))
        {
            /*SelectFont(gSet.mh_Font);
            TEXTMETRIC tm;
            GetTextMetrics(hDC, &tm);
            if (gSet.isForceMonospace)
                //Maximus - у Arial'а например MaxWidth слишком большой
                gSet.LogFont.lfWidth = gSet.FontSizeX3 ? gSet.FontSizeX3 : tm.tmMaxCharWidth;
            else
                gSet.LogFont.lfWidth = tm.tmAveCharWidth;
            gSet.LogFont.lfHeight = tm.tmHeight;

            if (ghOpWnd) // устанавливать только при листании шрифта в настройке
                gSet.UpdateTTF ( (tm.tmMaxCharWidth - tm.tmAveCharWidth)>2 );*/

            Assert ( gSet.LogFont.lfWidth && gSet.LogFont.lfHeight );

            BOOL lbWasInitialized = TextWidth && TextHeight;
            // Посчитать новый размер в пикселях
            Width = TextWidth * gSet.LogFont.lfWidth;
            Height = TextHeight * gSet.LogFont.lfHeight;

            if (ghOpWnd)
                gConEmu.UpdateSizes();

            //if (!lbWasInitialized) // если зовется InitDC значит размер консоли изменился
            if (!abNoWndResize) {
                if (gConEmu.isActive(this))
                    gConEmu.OnSize(-1);
            }

            hBitmap = CreateCompatibleBitmap(hScreenDC, Width, Height);
            SelectObject(hDC, hBitmap);
        }
        ReleaseDC(0, hScreenDC);

        return hBitmap!=NULL;
    }

    return true;
}

void CVirtualConsole::DumpConsole()
{
    OPENFILENAME ofn; memset(&ofn,0,sizeof(ofn));
    WCHAR temp[MAX_PATH+5];
    ofn.lStructSize=sizeof(ofn);
    ofn.hwndOwner = ghWnd;
    ofn.lpstrFilter = _T("ConEmu dumps (*.con)\0*.con\0\0");
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = temp; temp[0] = 0;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Dump console...";
    ofn.lpstrDefExt = L"con";
    ofn.Flags = OFN_ENABLESIZING|OFN_NOCHANGEDIR
            | OFN_PATHMUSTEXIST|OFN_EXPLORER|OFN_HIDEREADONLY|OFN_OVERWRITEPROMPT;
    if (!GetSaveFileName(&ofn))
        return;
        
    HANDLE hFile = CreateFile(temp, GENERIC_WRITE, FILE_SHARE_READ,
        NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        DisplayLastError(_T("Can't create file!"));
        return;
    }
    DWORD dw;
    LPCTSTR pszTitle = gConEmu.GetTitle();
    WriteFile(hFile, pszTitle, _tcslen(pszTitle)*sizeof(*pszTitle), &dw, NULL);
    swprintf(temp, _T("\r\nSize: %ix%i\r\n"), TextWidth, TextHeight);
    WriteFile(hFile, temp, _tcslen(temp)*sizeof(TCHAR), &dw, NULL);
    WriteFile(hFile, ConChar, TextWidth * TextHeight * 2, &dw, NULL);
    WriteFile(hFile, ConAttr, TextWidth * TextHeight * 2, &dw, NULL);
    WriteFile(hFile, ConChar+(TextWidth * TextHeight), TextWidth * TextHeight * 2, &dw, NULL);
    WriteFile(hFile, ConAttr+(TextWidth * TextHeight), TextWidth * TextHeight * 2, &dw, NULL);
	if (mp_RCon) {
		mp_RCon->DumpConsole(hFile);
	}
    CloseHandle(hFile);
}

// Это символы рамок и др. спец. символы
//#define isCharBorder(inChar) (inChar>=0x2013 && inChar<=0x266B)
bool CVirtualConsole::isCharBorder(WCHAR inChar)
{
    if (inChar>=0x2013 && inChar<=0x266B)
        return true;
    else
        return false;
    //if (gSet.isFixFarBorders)
    //{
    //  //if (! (inChar > 0x2500 && inChar < 0x251F))
    //  if ( !(inChar > 0x2013/*En dash*/ && inChar < 0x266B/*Beamed Eighth Notes*/) /*&& inChar!=L'}'*/ )
    //      /*if (inChar != 0x2550 && inChar != 0x2502 && inChar != 0x2551 && inChar != 0x007D &&
    //      inChar != 0x25BC && inChar != 0x2593 && inChar != 0x2591 && inChar != 0x25B2 &&
    //      inChar != 0x2562 && inChar != 0x255F && inChar != 0x255A && inChar != 0x255D &&
    //      inChar != 0x2554 && inChar != 0x2557 && inChar != 0x2500 && inChar != 0x2534 && inChar != 0x2564) // 0x2520*/
    //      return false;
    //  else
    //      return true;
    //}
    //else
    //{
    //  if (inChar < 0x01F1 || inChar > 0x0400 && inChar < 0x045F || inChar > 0x2012 && inChar < 0x203D || /*? - not sure that optimal*/ inChar > 0x2019 && inChar < 0x2303 || inChar > 0x24FF && inChar < 0x266C)
    //      return false;
    //  else
    //      return true;
    //}
}

// А это только "рамочные" символы, в которых есть любая (хотя бы частичная) вертикальная черта + стрелки/штриховки
bool CVirtualConsole::isCharBorderVertical(WCHAR inChar)
{
    //if (inChar>=0x2502 && inChar<=0x25C4 && inChar!=0x2550)
    if (inChar==0x2502 || inChar==0x2503 || inChar==0x2506 || inChar==0x2507
        || (inChar>=0x250A && inChar<=0x254B) || inChar==0x254E || inChar==0x254F
        || (inChar>=0x2551 && inChar<=0x25C5)) // По набору символов Arial Unicode MS
        return true;
    else
        return false;
}

void CVirtualConsole::BlitPictureTo(int inX, int inY, int inWidth, int inHeight)
{
    // А вообще, имеет смысл отрисовывать?
    if (gSet.bgBmp.X>inX && gSet.bgBmp.Y>inY)
        BitBlt(hDC, inX, inY, inWidth, inHeight, gSet.hBgDc, inX, inY, SRCCOPY);
    if (gSet.bgBmp.X<(inX+inWidth) || gSet.bgBmp.Y<(inY+inHeight))
    {
        if (hBrush0==NULL) {
            hBrush0 = CreateSolidBrush(gSet.Colors[0]);
            SelectBrush(hBrush0);
        }

        RECT rect = {max(inX,gSet.bgBmp.X), inY, inX+inWidth, inY+inHeight};
        if (!IsRectEmpty(&rect))
            FillRect(hDC, &rect, hBrush0);

        if (gSet.bgBmp.X>inX) {
            rect.left = inX; rect.top = max(inY,gSet.bgBmp.Y); rect.right = gSet.bgBmp.X;
            if (!IsRectEmpty(&rect))
                FillRect(hDC, &rect, hBrush0);
        }

        //DeleteObject(hBrush);
    }
}

void CVirtualConsole::SelectFont(HFONT hNew)
{
    if (!hNew) {
        if (hOldFont)
            SelectObject(hDC, hOldFont);
        hOldFont = NULL;
        hSelectedFont = NULL;
    } else
    if (hSelectedFont != hNew)
    {
        hSelectedFont = (HFONT)SelectObject(hDC, hNew);
        if (!hOldFont)
            hOldFont = hSelectedFont;
        hSelectedFont = hNew;
    }
}

void CVirtualConsole::SelectBrush(HBRUSH hNew)
{
    if (!hNew) {
        if (hOldBrush)
            SelectObject(hDC, hOldBrush);
        hOldBrush = NULL;
    } else
    if (hSelectedBrush != hNew)
    {
        hSelectedBrush = (HBRUSH)SelectObject(hDC, hNew);
        if (!hOldBrush)
            hOldBrush = hSelectedBrush;
        hSelectedBrush = hNew;
    }
}

bool CVirtualConsole::CheckSelection(const CONSOLE_SELECTION_INFO& select, SHORT row, SHORT col)
{
    if ((select.dwFlags & CONSOLE_SELECTION_NOT_EMPTY) == 0)
        return false;
    if (row < select.srSelection.Top || row > select.srSelection.Bottom)
        return false;
    if (col < select.srSelection.Left || col > select.srSelection.Right)
        return false;
    return true;
}

#ifdef MSGLOGGER
class DcDebug {
public:
    DcDebug(HDC* ahDcVar, HDC* ahPaintDC) {
        mb_Attached=FALSE; mh_OrigDc=NULL; mh_DcVar=NULL; mh_Dc=NULL;
        if (!ahDcVar || !ahPaintDC) return;
        mh_DcVar = ahDcVar;
        mh_OrigDc = *ahDcVar;
        mh_Dc = *ahPaintDC;
        *mh_DcVar = mh_Dc;
    };
    ~DcDebug() {
        if (mb_Attached && mh_DcVar) {
            mb_Attached = FALSE;
            *mh_DcVar = mh_OrigDc;
        }
    };
protected:
    BOOL mb_Attached;
    HDC mh_OrigDc, mh_Dc;
    HDC* mh_DcVar;
};
#endif

// Возвращает true, если процедура изменила отображаемый символ
bool CVirtualConsole::GetCharAttr(TCHAR ch, WORD atr, TCHAR& rch, BYTE& foreColorNum, BYTE& backColorNum)
{
    bool bChanged = false;
    foreColorNum = atr & 0x0F;
    backColorNum = atr >> 4 & 0x0F;
    rch = ch; // по умолчанию!
    if (isEditor && gSet.isVisualizer && ch==L' ' &&
        (backColorNum==gSet.nVizTab || backColorNum==gSet.nVizEOL || backColorNum==gSet.nVizEOF))
    {
        if (backColorNum==gSet.nVizTab)
            rch = gSet.cVizTab; else
        if (backColorNum==gSet.nVizEOL)
            rch = gSet.cVizEOL; else
        if (backColorNum==gSet.nVizEOF)
            rch = gSet.cVizEOF;
        backColorNum = gSet.nVizNormal;
        foreColorNum = gSet.nVizFore;
        bChanged = true;
    } else
    if (gSet.isExtendColors) {
        if (backColorNum==gSet.nExtendColor) {
            backColorNum = attrBackLast;
            foreColorNum += 0x10;
        } else {
            attrBackLast = backColorNum;
        }
    }
    return bChanged;
}

// Возвращает ширину символа, учитывает FixBorders
WORD CVirtualConsole::CharWidth(TCHAR ch)
{
	if (gSet.isForceMonospace || !gSet.isProportional)
        return gSet.LogFont.lfWidth;

    WORD nWidth = gSet.LogFont.lfWidth;
    bool isBorder = false; //, isVBorder = false;

    if (gSet.isFixFarBorders) {
        isBorder = isCharBorder(ch);
        //if (isBorder) {
        //  isVBorder = ch == 0x2551 /*Light Vertical*/ || ch == 0x2502 /*Double Vertical*/;
        //}
    }

    //if (isBorder) {
        //if (!Font2Width[ch]) {
        //  if (!isVBorder) {
        //      Font2Width[ch] = gSet.LogFont.lfWidth;
        //  } else {
        //      SelectFont(hFont2);
        //      SIZE sz;
        //      if (GetTextExtentPoint32(hDC, &ch, 1, &sz) && sz.cx)
        //          Font2Width[ch] = sz.cx;
        //      else
        //          Font2Width[ch] = gSet.LogFont2.lfWidth;
        //  }
        //}
        //nWidth = Font2Width[ch];
    //} else {
    if (!isBorder) {
        if (!gSet.FontWidth[ch]) {
            SelectFont(gSet.mh_Font);
            SIZE sz;
            ABC abc;
            //This function succeeds only with TrueType fonts
            BOOL lb1 = GetCharABCWidths(hDC, ch, ch, &abc);

            if (GetTextExtentPoint32(hDC, &ch, 1, &sz) && sz.cx)
                gSet.FontWidth[ch] = sz.cx;
            else
                gSet.FontWidth[ch] = nWidth;
        }
        nWidth = gSet.FontWidth[ch];
    }
    if (!nWidth) nWidth = 1; // на всякий случай, чтобы деления на 0 не возникло
    return nWidth;
}

bool CVirtualConsole::CheckChangedTextAttr()
{
#ifdef MSGLOGGER
    COORD ch;
    if (textChanged) {
        for (UINT i=0,j=TextLen; i<TextLen; i++,j++) {
            if (ConChar[i] != ConChar[j]) {
                ch.Y = i % TextWidth;
                ch.X = i - TextWidth * ch.Y;
                break;
            }
        }
    }
    if (attrChanged) {
        for (UINT i=0,j=TextLen; i<TextLen; i++,j++) {
            if (ConAttr[i] != ConAttr[j]) {
                ch.Y = i % TextWidth;
                ch.X = i - TextWidth * ch.Y;
                break;
            }
        }
    }
#endif

    textChanged = 0!=memcmp(ConChar + TextLen, ConChar, TextLen * sizeof(*ConChar));
    attrChanged = 0!=memcmp(ConAttr + TextLen, ConAttr, TextLen * sizeof(*ConAttr));

    return textChanged || attrChanged;
}

bool CVirtualConsole::Update(bool isForce, HDC *ahDc)
{
    if (!this || !mp_RCon || !mp_RCon->hConWnd)
        return false;

	if (!mp_RCon->IsConsoleThread()) {
		if (!ahDc) {
			mp_RCon->SetForceRead();
			mp_RCon->WaitEndUpdate(1000);
			//gConEmu.InvalidateAll(); -- зачем на All??? Update и так Invalidate зовет
			return false;
		}
    }

    //RetrieveConsoleInfo();

    #ifdef MSGLOGGER
    DcDebug dbg(&hDC, ahDc); // для отладки - рисуем сразу на канвасе окна
    #endif

    // переоткрыть хэндл Output'а при необходимости
    /*if (!hConOut(TRUE)) {
        return false;
    }*/

    MCHKHEAP
    bool lRes = false;
    
    CSection SCON(&csCON, &ncsTCON);
    //CSection SDC(&csDC, &ncsTDC);

    GetConsoleScreenBufferInfo(&csbi);

    // start timer before ReadConsoleOutput* calls, they do take time
    //gSet.Performance(tPerfRead, FALSE);

    //if (gbNoDblBuffer) isForce = TRUE; // Debug, dblbuffer

    //------------------------------------------------------------------------
    ///| Read console output and cursor info... |/////////////////////////////
    //------------------------------------------------------------------------
    if (!UpdatePrepare(isForce, ahDc)) {
        gConEmu.DnDstep(_T("DC initialization failed!"));
        return false;
    }
    
    //gSet.Performance(tPerfRead, TRUE);

    //gSet.Performance(tPerfRender, FALSE);

    //------------------------------------------------------------------------
    ///| Drawing text (if there were changes in console) |////////////////////
    //------------------------------------------------------------------------
    bool updateText, updateCursor;
    if (isForce)
    {
        updateText = updateCursor = attrChanged = textChanged = true;
    }
    else
    {

        // Do we have to update changed text?
        updateText = doSelect || CheckChangedTextAttr();
            //(textChanged = 0!=memcmp(ConChar + TextLen, ConChar, TextLen * sizeof(TCHAR))) || 
            //(attrChanged = 0!=memcmp(ConAttr + TextLen, ConAttr, TextLen * 2));
        
        // Do we have to update text under changed cursor?
        // Important: check both 'cinf.bVisible' and 'Cursor.isVisible',
        // because console may have cursor hidden and its position changed -
        // in this case last visible cursor remains shown at its old place.
        // Also, don't check foreground window here for the same reasons.
        // If position is the same then check the cursor becomes hidden.
        if (Cursor.x != csbi.dwCursorPosition.X || Cursor.y != csbi.dwCursorPosition.Y)
            // сменилась позиция. обновляем если курсор видим
            updateCursor = cinf.bVisible || Cursor.isVisible || Cursor.isVisiblePrevFromInfo;
        else
            updateCursor = Cursor.isVisiblePrevFromInfo && !cinf.bVisible;
    }
    
    gSet.Performance(tPerfRender, FALSE);

    if (updateText /*|| updateCursor*/)
    {
        lRes = true;

        //------------------------------------------------------------------------
        ///| Drawing modified text |//////////////////////////////////////////////
        //------------------------------------------------------------------------
        UpdateText(isForce, updateText, updateCursor);


        //MCHKHEAP
        //------------------------------------------------------------------------
        ///| Now, store data for further comparison |/////////////////////////////
        //------------------------------------------------------------------------
        //if (updateText)
        {
            memcpy(ConChar + TextLen, ConChar, TextLen * sizeof(TCHAR));
            memcpy(ConAttr + TextLen, ConAttr, TextLen * 2);
        }
    }

    //MCHKHEAP
    //------------------------------------------------------------------------
    ///| Drawing cursor |/////////////////////////////////////////////////////
    //------------------------------------------------------------------------
    UpdateCursor(lRes);
    
    MCHKHEAP
    
    //SDC.Leave();
    SCON.Leave();

	if (lRes && gConEmu.isActive(this))
		gConEmu.m_Child.Invalidate();

    gSet.Performance(tPerfRender, TRUE);

    /* ***************************************** */
    /*       Finalization, release objects       */
    /* ***************************************** */
    SelectBrush(NULL);
    if (hBrush0) {
        DeleteObject(hBrush0); hBrush0=NULL;
    }
    SelectFont(NULL);
    MCHKHEAP
    return lRes;
}

bool CVirtualConsole::UpdatePrepare(bool isForce, HDC *ahDc)
{
    CSection S(&csCON, &ncsTCON);
    
    attrBackLast = 0;
    isEditor = gConEmu.isEditor();
    isFilePanel = gConEmu.isFilePanel(true);

    //winSize.X = csbi.srWindow.Right - csbi.srWindow.Left + 1; winSize.Y = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    //if (winSize.X < csbi.dwSize.X)
    //    winSize.X = csbi.dwSize.X;
    winSize = MakeCoord(mp_RCon->TextWidth(),mp_RCon->TextHeight());

    //csbi.dwCursorPosition.X -= csbi.srWindow.Left; -- горизонтальная прокрутка игнорируется!
    csbi.dwCursorPosition.Y -= csbi.srWindow.Top;
    isCursorValid =
        csbi.dwCursorPosition.X >= 0 && csbi.dwCursorPosition.Y >= 0 &&
        csbi.dwCursorPosition.X < winSize.X && csbi.dwCursorPosition.Y < winSize.Y;

    if (mb_RequiredForceUpdate || mb_IsForceUpdate) {
	    isForce = true;
	    mb_RequiredForceUpdate = false;
    }   

    // Первая инициализация, или смена размера
    if (isForce || !ConChar || TextWidth != winSize.X || TextHeight != winSize.Y) {
        if (!InitDC(ahDc!=NULL && !isForce/*abNoDc*/, false/*abNoWndResize*/))
            return false;
    }

    // Требуется полная перерисовка!
    if (mb_IsForceUpdate)
    {
        isForce = true;
        mb_IsForceUpdate = false;
    }
    // Отладочный режим. Двойная буферизация отключена, отрисовка идет сразу на ahDc
    if (ahDc)
        isForce = true;


    drawImage = gSet.isShowBgImage && gSet.isBackgroundImageValid;
    TextLen = TextWidth * TextHeight;
    coord.X = csbi.srWindow.Left; coord.Y = csbi.srWindow.Top;

    // скопировать данные из состояния консоли В ConAttr/ConChar
	mp_RCon->GetData(ConChar, ConAttr, TextLen*2);

    HEAPVAL
 
    
    // Определить координаты панелей
    mr_LeftPanel = mr_RightPanel = MakeRect(-1,-1);
    if (gConEmu.isFar() && TextHeight >= MIN_CON_HEIGHT && TextWidth >= MIN_CON_WIDTH)
    {
        uint nY = 0;
        if (ConChar[0] == L' ')
            nY ++; // скорее всего, первая строка - меню
        else if (ConChar[0] == L'R' && ConChar[1] == L' ')
            nY ++; // скорее всего, первая строка - меню, при включенной записи макроса
            
        uint nIdx = nY*TextWidth;
        
        if (( (ConChar[nIdx] == L'[' && (ConChar[nIdx+1]>=L'0' && ConChar[nIdx+1]<=L'9')) // открыто несколько редакторов/вьюверов
              || (ConChar[nIdx] == 0x2554 && ConChar[nIdx+1] == 0x2550) // доп.окон нет, только рамка
            ) && ConChar[nIdx+TextWidth] == 0x2551)
        {
            LPCWSTR pszCenter = ConChar + nIdx;
            LPCWSTR pszLine = ConChar + nIdx;
            uint nCenter = 0;
            while ( (pszCenter = wcsstr(pszCenter+1, L"\x2557\x2554")) != NULL ) {
                nCenter = pszCenter - pszLine;
                if (ConChar[nIdx+TextWidth+nCenter] == 0x2551 && ConChar[nIdx+TextWidth+nCenter+1] == 0x2551) {
                    break; // нашли
                }
            }
            
            uint nBottom = TextHeight - 1;
            while (nBottom > 4) {
                if (ConChar[TextWidth*nBottom] == 0x255A && ConChar[TextWidth*(nBottom-1)] == 0x2551)
                    break;
                nBottom --;
            }
            
            if (pszCenter && nBottom > 4) {
                mr_LeftPanel.left = 1;
                mr_LeftPanel.top = nY + 2;
                mr_LeftPanel.right = nCenter - 1;
                mr_LeftPanel.bottom = nBottom - 3;
                
                mr_RightPanel.left = nCenter + 3;
                mr_RightPanel.top = nY + 2;
                mr_RightPanel.right = TextWidth - 2;
                mr_RightPanel.bottom = mr_LeftPanel.bottom;
            }
        }
    }

    // get cursor info
    GetConsoleCursorInfo(/*hConOut(),*/ &cinf);

    // get selection info in buffer mode
    
    doSelect = mp_RCon > 0;
    if (doSelect)
    {
        select1 = SelectionInfo;
        GetConsoleSelectionInfo(&select2);
        select2.srSelection.Top -= csbi.srWindow.Top;
        select2.srSelection.Bottom -= csbi.srWindow.Top;
        SelectionInfo = select2;
        doSelect = (select1.dwFlags & CONSOLE_SELECTION_NOT_EMPTY) || (select2.dwFlags & CONSOLE_SELECTION_NOT_EMPTY);
        if (doSelect)
        {
            if (select1.dwFlags == select2.dwFlags &&
                select1.srSelection.Top == select2.srSelection.Top &&
                select1.srSelection.Left == select2.srSelection.Left &&
                select1.srSelection.Right == select2.srSelection.Right &&
                select1.srSelection.Bottom == select2.srSelection.Bottom)
            {
                doSelect = false;
            }
        }
    }

    MCHKHEAP
    return true;
}

enum CVirtualConsole::_PartType CVirtualConsole::GetCharType(TCHAR ch)
{
    enum _PartType cType = pNull;

    if (ch == L' ')
        cType = pSpace;
    //else if (ch == L'_')
    //  cType = pUnderscore;
    else if (isCharBorder(ch)) {
        if (isCharBorderVertical(ch))
            cType = pVBorder;
        else
            cType = pBorder;
    }
    else if (isFilePanel && ch == L'}')
        cType = pRBracket;
    else
        cType = pText;

    return cType;
}

// row - 0-based
void CVirtualConsole::ParseLine(int row, TCHAR *ConCharLine, WORD *ConAttrLine)
{
    UINT idx = 0;
    struct _TextParts *pStart=TextParts, *pEnd=TextParts;
    enum _PartType cType1, cType2;
    UINT i1=0, i2=0;
    
    pEnd->partType = pNull; // сразу ограничим строку
    
    TCHAR ch1, ch2;
    BYTE af1, ab1, af2, ab2;
    DWORD pixels;
    while (i1<TextWidth)
    {
        GetCharAttr(ConCharLine[i1], ConAttrLine[i1], ch1, af1, ab1);
        cType1 = GetCharType(ch1);
        if (cType1 == pRBracket) {
            if (!(row>=2 && isCharBorderVertical(ConChar[TextWidth+i1]))
                && (((UINT)row)<=(TextHeight-4)))
                cType1 = pText;
        }
        pixels = CharWidth(ch1);

        i2 = i1+1;
        // в режиме Force Monospace отрисовка идет по одному символу
        if (!gSet.isForceMonospace && i2 < TextWidth && 
            (cType1 != pVBorder && cType1 != pRBracket))
        {
            GetCharAttr(ConCharLine[i2], ConAttrLine[i2], ch2, af2, ab2);
            // Получить блок символов с аналогичными цветами
            while (i2 < TextWidth && af2 == af1 && ab2 == ab1) {
                // если символ отличается от первого

                cType2 = GetCharType(ch2);
                if ((ch2 = ConCharLine[i2]) != ch1) {
                    if (cType2 == pRBracket) {
                        if (!(row>=2 && isCharBorderVertical(ConChar[TextWidth+i2]))
                            && (((UINT)row)<=(TextHeight-4)))
                            cType2 = pText;
                    }

                    // и он вообще из другой группы
                    if (cType2 != cType1)
                        break; // то завершаем поиск
                }
                pixels += CharWidth(ch2); // добавить ширину символа в пикселях
                i2++; // следующий символ
                GetCharAttr(ConCharLine[i2], ConAttrLine[i2], ch2, af2, ab2);
                if (cType2 == pRBracket) {
                    if (!(row>=2 && isCharBorderVertical(ConChar[TextWidth+i2]))
                        && (((UINT)row)<=(TextHeight-4)))
                        cType2 = pText;
                }
            }
        }

        // при разборе строки будем смотреть, если нашли pText,pSpace,pText то pSpace,pText добавить в первый pText
        if (cType1 == pText && (pEnd - pStart) >= 2) {
            if (pEnd[-1].partType == pSpace && pEnd[-2].partType == pText &&
                pEnd[-1].attrBack == ab1 && pEnd[-1].attrFore == af1 &&
                pEnd[-2].attrBack == ab1 && pEnd[-2].attrFore == af1
                )
            {   
                pEnd -= 2;
                pEnd->i2 = i2 - 1;
                pEnd->iwidth = i2 - pEnd->i1;
                pEnd->width += pEnd[1].width + pixels;
                pEnd ++;
                i1 = i2;
                continue;
            }
        }
        pEnd->i1 = i1; pEnd->i2 = i2 - 1; // конец "включая"
        pEnd->partType = cType1;
        pEnd->attrBack = ab1; pEnd->attrFore = af1;
        pEnd->iwidth = i2 - i1;
        pEnd->width = pixels;
        if (gSet.isForceMonospace ||
            (gSet.isProportional && (cType1 == pVBorder || cType1 == pRBracket)))
        {
            pEnd->x1 = i1 * gSet.LogFont.lfWidth;
        } else {
            pEnd->x1 = -1;
        }

        pEnd ++; // блоков не может быть больше количества символов в строке, так что с размерностью все ОК
        i1 = i2;
    }
    // пока поставим конец блоков, потом, если ширины не хватит - добавим pDummy
    pEnd->partType = pNull;
}

void CVirtualConsole::UpdateText(bool isForce, bool updateText, bool updateCursor)
{
    if (!updateText)
        return;

    SelectFont(gSet.mh_Font);

    // pointers
    TCHAR* ConCharLine;
    WORD* ConAttrLine;
    DWORD* ConCharXLine;
    // counters
    int pos, row;
    {
        int i;
        if (updateText)
        {
            i = 0; //TextLen - TextWidth; // TextLen = TextWidth/*80*/ * TextHeight/*25*/;
            pos = 0; //Height - gSet.LogFont.lfHeight; // Height = TextHeight * gSet.LogFont.lfHeight;
            row = 0; //TextHeight - 1;
        }
        else
        { // по идее, сюда вообще не доходим
            i = TextWidth * Cursor.y;
            pos = gSet.LogFont.lfHeight * Cursor.y;
            row = Cursor.y;
        }
        ConCharLine = ConChar + i;
        ConAttrLine = ConAttr + i;
        ConCharXLine = ConCharX + i;
    }
    int nMaxPos = Height - gSet.LogFont.lfHeight;

    if (/*gSet.isForceMonospace ||*/ !drawImage)
        SetBkMode(hDC, OPAQUE);
    else
        SetBkMode(hDC, TRANSPARENT);

    // rows
    //TODO: а зачем в isForceMonospace принудительно перерисовывать все?
    const bool skipNotChanged = !isForce /*&& !gSet.isForceMonospace*/;
    for (; pos <= nMaxPos; 
        ConCharLine += TextWidth, ConAttrLine += TextWidth, ConCharXLine += TextWidth,
        pos += gSet.LogFont.lfHeight, row++)
    {
        // the line
        const WORD* const ConAttrLine2 = ConAttrLine + TextLen;
        const TCHAR* const ConCharLine2 = ConCharLine + TextLen;

        // skip not changed symbols except the old cursor or selection
        int j = 0, end = TextWidth;
        if (skipNotChanged)
        {
            // *) Skip not changed tail symbols.
            while(--end >= 0 && ConCharLine[end] == ConCharLine2[end] && ConAttrLine[end] == ConAttrLine2[end])
            {
                if (updateCursor && row == Cursor.y && end == Cursor.x)
                    break;
                if (doSelect)
                {
                    if (CheckSelection(select1, row, end))
                        break;
                    if (CheckSelection(select2, row, end))
                        break;
                }
            }
            if (end < j)
                continue;
            ++end;

            // *) Skip not changed head symbols.
            while(j < end && ConCharLine[j] == ConCharLine2[j] && ConAttrLine[j] == ConAttrLine2[j])
            {
                if (updateCursor && row == Cursor.y && j == Cursor.x)
                    break;
                if (doSelect)
                {
                    if (CheckSelection(select1, row, j))
                        break;
                    if (CheckSelection(select2, row, j))
                        break;
                }
                ++j;
            }
            if (j >= end)
                continue;
        }
        
        if (Cursor.isVisiblePrev && row == Cursor.y
            && (j <= Cursor.x && Cursor.x <= end))
            Cursor.isVisiblePrev = false;

        // *) Now draw as much as possible in a row even if some symbols are not changed.
        // More calls for the sake of fewer symbols is slower, e.g. in panel status lines.
        int j2=j+1;
        for (; j < end; j = j2)
        {
            const WORD attr = ConAttrLine[j];
            WCHAR c = ConCharLine[j];
            BYTE attrFore, attrBack;
            bool isUnicode = isCharBorder(c/*ConCharLine[j]*/);
            bool lbS1 = false, lbS2 = false;
            int nS11 = 0, nS12 = 0, nS21 = 0, nS22 = 0;

            if (GetCharAttr(c, attr, c, attrFore, attrBack))
                isUnicode = true;

            MCHKHEAP
            // корректировка лидирующих пробелов и рамок
            if (gSet.isProportional && (c==0x2550 || c==0x2500)) // 'Box light horizontal' & 'Box double horizontal' - это всегда
            {
                lbS1 = true; nS11 = nS12 = j;
                while ((nS12 < end) && (ConCharLine[nS12+1] == c))
                    nS12 ++;
                // Посчитать сколько этих же символов ПОСЛЕ текста
                if (nS12<end) {
                    nS21 = nS12+1; // Это должен быть НЕ c 
                    // ищем первый "рамочный" символ
                    while ((nS21<end) && (ConCharLine[nS21] != c) && !isCharBorder(ConCharLine[nS21]))
                        nS21 ++;
                    if (nS21<end && ConCharLine[nS21]==c) {
                        lbS2 = true; nS22 = nS21;
                        while ((nS22 < end) && (ConCharLine[nS22+1] == c))
                            nS22 ++;
                    }
                }
            } MCHKHEAP
            // а вот для пробелов - когда их более одного
            /*else if (c==L' ' && j<end && ConCharLine[j+1]==L' ')
            {
                lbS1 = true; nS11 = nS12 = j;
                while ((nS12 < end) && (ConCharLine[nS12+1] == c))
                    nS12 ++;
            }*/

            // Возможно это тоже нужно вынести в GetCharAttr?
            if (doSelect && CheckSelection(select2, row, j))
            {
                WORD tmp = attrFore;
                attrFore = attrBack;
                attrBack = tmp;
            }

            SetTextColor(hDC, gSet.Colors[attrFore]);

            // корректировка положения вертикальной рамки
            if (gSet.isProportional && j)
            {
                MCHKHEAP
                DWORD nPrevX = ConCharXLine[j-1];
                if (isCharBorderVertical(c)) {
                    //2009-04-21 было (j-1) * gSet.LogFont.lfWidth;
                    ConCharXLine[j-1] = j * gSet.LogFont.lfWidth;
                } else if (isFilePanel && c==L'}') {
                    // Символом } фар помечает имена файлов вылезшие за пределы колонки...
                    // Если сверху или снизу на той же позиции рамки (или тот же '}')
                    // корректируем позицию
                    if ((row>=2 && isCharBorderVertical(ConChar[TextWidth+j]))
                        && (((UINT)row)<=(TextHeight-4)))
                        //2009-04-21 было (j-1) * gSet.LogFont.lfWidth;
                        ConCharXLine[j-1] = j * gSet.LogFont.lfWidth;
                    //row = TextHeight - 1;
                } MCHKHEAP
                if (nPrevX < ConCharXLine[j-1]) {
                    // Требуется зарисовать пропущенную область. пробелами что-ли?

                    RECT rect;
                    rect.left = nPrevX;
                    rect.top = pos;
                    rect.right = ConCharXLine[j-1];
                    rect.bottom = rect.top + gSet.LogFont.lfHeight;

                    if (gbNoDblBuffer) GdiFlush();
                    if (! (drawImage && (attrBack) < 2)) {
                        SetBkColor(hDC, gSet.Colors[attrBack]);
                        int nCnt = ((rect.right - rect.left) / CharWidth(L' '))+1;

                        UINT nFlags = ETO_CLIPPED; // || ETO_OPAQUE;
                        ExtTextOut(hDC, rect.left, rect.top, nFlags, &rect,
                            Spaces, min(nSpaceCount, nCnt), 0);

                    } else if (drawImage) {
                        BlitPictureTo(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
                    } MCHKHEAP
                    if (gbNoDblBuffer) GdiFlush();
                }
            }

            ConCharXLine[j] = (j ? ConCharXLine[j-1] : 0)+CharWidth(c);
            MCHKHEAP


            if (gSet.isForceMonospace)
            {
                MCHKHEAP
                SetBkColor(hDC, gSet.Colors[attrBack]);

                j2 = j + 1;

                if (gSet.isFixFarBorders) {
                    if (!isUnicode)
                        SelectFont(gSet.mh_Font);
                    else if (isUnicode)
                        SelectFont(gSet.mh_Font2);
                }


                RECT rect;
                if (!gSet.isProportional)
                    rect = MakeRect(j * gSet.LogFont.lfWidth, pos, j2 * gSet.LogFont.lfWidth, pos + gSet.LogFont.lfHeight);
                else {
                    rect.left = j ? ConCharXLine[j-1] : 0;
                    rect.top = pos;
                    rect.right = (TextWidth>(UINT)j2) ? ConCharXLine[j2-1] : Width;
                    rect.bottom = rect.top + gSet.LogFont.lfHeight;
                }
                UINT nFlags = ETO_CLIPPED | ((drawImage && (attrBack) < 2) ? 0 : ETO_OPAQUE);
                int nShift = 0;

                MCHKHEAP
                if (c != 0x20 && !isUnicode) {
                    ABC abc;
                    //This function succeeds only with TrueType fonts
                    if (GetCharABCWidths(hDC, c, c, &abc))
                    {
                        
                        if (abc.abcA<0) {
                            // иначе символ наверное налезет на предыдущий?
                            nShift = -abc.abcA;
                        } else if (abc.abcA<(((int)gSet.LogFont.lfWidth-(int)abc.abcB-1)/2)) {
                            // символ I, i, и др. очень тонкие - рисуем посередине
                            nShift = ((gSet.LogFont.lfWidth-abc.abcB)/2)-abc.abcA;
                        }
                    }
                }

                MCHKHEAP
                if (! (drawImage && (attrBack) < 2)) {
                    SetBkColor(hDC, gSet.Colors[attrBack]);
                    //TODO: надо раскомментарить и куда-то приткнуть...
                    if (nShift>0) ExtTextOut(hDC, rect.left, rect.top, nFlags, &rect, L" ", 1, 0);
                } else if (drawImage) {
                    BlitPictureTo(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
                }

                if (nShift>0) {
                    rect.left += nShift; rect.right += nShift;
                }

                if (gbNoDblBuffer) GdiFlush();
                ExtTextOut(hDC, rect.left, rect.top, nFlags, &rect, &c, 1, 0);
                if (gbNoDblBuffer) GdiFlush();
                MCHKHEAP

            }
            else if (gSet.isProportional && c==L' ')
            {
                j2 = j + 1; MCHKHEAP
                if (!doSelect) // doSelect инициализируется только для BufferHeight>0
                {
                    TCHAR ch;
                    while(j2 < end && ConAttrLine[j2] == attr && (ch=ConCharLine[j2]) == L' ')
                    {
                        ConCharXLine[j2] = (j2 ? ConCharXLine[j2-1] : 0)+CharWidth(ch);
                        j2++;
                    } MCHKHEAP
                    if (j2>=(int)TextWidth || isCharBorderVertical(ConCharLine[j2])) //ch может быть не инициализирован
                    {
                        ConCharXLine[j2-1] = (j2>=(int)TextWidth) ? Width : (j2) * gSet.LogFont.lfWidth; // или тут [j] должен быть?
                        MCHKHEAP
                        DWORD n1 = ConCharXLine[j];
                        DWORD n3 = j2-j; // кол-во символов
                        DWORD n2 = ConCharXLine[j2-1] - n1; // выделенная на пробелы ширина
                        MCHKHEAP

                        for (int k=j+1; k<(j2-1); k++) {
                            ConCharXLine[k] = n1 + (n3 ? klMulDivU32(k-j, n2, n3) : 0);
                            MCHKHEAP
                        }
                    } MCHKHEAP
                }
                if (gSet.isFixFarBorders)
                    SelectFont(gSet.mh_Font);
            }
            else if (!isUnicode)
            {
                j2 = j + 1; MCHKHEAP
                if (!doSelect) // doSelect инициализируется только для BufferHeight>0
                {
                    #ifndef DRAWEACHCHAR
                    // Если этого не делать - в пропорциональных шрифтах буквы будут наезжать одна на другую
                    TCHAR ch;
                    while(j2 < end && ConAttrLine[j2] == attr && 
                        !isCharBorder(ch = ConCharLine[j2]) 
                        && (!gSet.isProportional || !isFilePanel || (ch != L'}' && ch!=L' '))) // корректировка имен в колонках
                    {
                        ConCharXLine[j2] = (j2 ? ConCharXLine[j2-1] : 0)+CharWidth(ch);
                        j2++;
                    }
                    #endif
                }
                if (gSet.isFixFarBorders)
                    SelectFont(gSet.mh_Font);
                MCHKHEAP
            }
            else //Border and specials
            {
                j2 = j + 1; MCHKHEAP
                if (!doSelect) // doSelect инициализируется только для BufferHeight>0
                {
                    if (!gSet.isFixFarBorders)
                    {
                        TCHAR ch;
                        while(j2 < end && ConAttrLine[j2] == attr && isCharBorder(ch = ConCharLine[j2]))
                        {
                            ConCharXLine[j2] = (j2 ? ConCharXLine[j2-1] : 0)+CharWidth(ch);
                            j2++;
                        }
                    }
                    else
                    {
                        TCHAR ch;
                        while(j2 < end && ConAttrLine[j2] == attr && 
                            isCharBorder(ch = ConCharLine[j2]) && ch == ConCharLine[j2+1])
                        {
                            ConCharXLine[j2] = (j2 ? ConCharXLine[j2-1] : 0)+CharWidth(ch);
                            j2++;
                        }
                    }
                }
                if (gSet.isFixFarBorders)
                    SelectFont(gSet.mh_Font2);
                MCHKHEAP
            }

            if (!gSet.isForceMonospace)
            {
                RECT rect;
                if (!gSet.isProportional)
                    rect = MakeRect(j * gSet.LogFont.lfWidth, pos, j2 * gSet.LogFont.lfWidth, pos + gSet.LogFont.lfHeight);
                else {
                    rect.left = j ? ConCharXLine[j-1] : 0;
                    rect.top = pos;
                    rect.right = (TextWidth>(UINT)j2) ? ConCharXLine[j2-1] : Width;
                    rect.bottom = rect.top + gSet.LogFont.lfHeight;
                }

                MCHKHEAP
                if (! (drawImage && (attrBack) < 2))
                    SetBkColor(hDC, gSet.Colors[attrBack]);
                else if (drawImage)
                    BlitPictureTo(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);

                UINT nFlags = ETO_CLIPPED | ((drawImage && (attrBack) < 2) ? 0 : ETO_OPAQUE);

                MCHKHEAP
                if (gbNoDblBuffer) GdiFlush();
                if (gSet.isProportional && c == ' ') {
                    int nCnt = ((rect.right - rect.left) / CharWidth(L' '))+1;
                    ExtTextOut(hDC, rect.left, rect.top, nFlags, &rect,
                        Spaces, nCnt, 0);
                } else
                if (gSet.LogFont.lfCharSet == OEM_CHARSET && !isUnicode)
                {
                    WideCharToMultiByte(CP_OEMCP, 0, ConCharLine + j, j2 - j, tmpOem, TextWidth+4, 0, 0);
                    ExtTextOutA(hDC, rect.left, rect.top, nFlags,
                        &rect, tmpOem, j2 - j, 0);
                }
                else
                {
                    if ((j2-j)==1) // support visualizer change
                    ExtTextOut(hDC, rect.left, rect.top, nFlags, &rect,
                        &c/*ConCharLine + j*/, 1, 0);
                    else
                    ExtTextOut(hDC, rect.left, rect.top, nFlags, &rect,
                        ConCharLine + j, j2 - j, 0);
                }
                if (gbNoDblBuffer) GdiFlush();
                MCHKHEAP
            }
        
            // stop if all is done
            if (!updateText)
                goto done;

            // skip next not changed symbols again
            if (skipNotChanged)
            {
                MCHKHEAP
                // skip the same except the old cursor
                while(j2 < end && ConCharLine[j2] == ConCharLine2[j2] && ConAttrLine[j2] == ConAttrLine2[j2])
                {
                    if (updateCursor && row == Cursor.y && j2 == Cursor.x)
                        break;
                    if (doSelect)
                    {
                        if (CheckSelection(select1, row, j2))
                            break;
                        if (CheckSelection(select2, row, j2))
                            break;
                    }
                    ++j2;
                }
            }
        }
        if (!updateText)
            break; // только обновление курсора? а нахрена тогда UpdateText вызывать...
    }
done:
    return;
}

void CVirtualConsole::UpdateCursorDraw(COORD pos, BOOL vis, UINT dwSize, LPRECT prcLast/*=NULL*/)
{
    int CurChar = pos.Y * TextWidth + pos.X;
    if (CurChar < 0 || CurChar>=(int)(TextWidth * TextHeight))
        return; // может быть или глюк - или размер консоли был резко уменьшен и предыдущая позиция курсора пропала с экрана
    if (!ConCharX)
        return; // защита
    COORD pix;
    /*if (prcLast) {
        //pix = MakeCoord(prcLast->left, prcLast->top);
        blitSize = MakeCoord(prcLast->right-prcLast->left, prcLast->bottom-prcLast->top);
    } else */
    {
        pix.X = pos.X * gSet.LogFont.lfWidth;
        pix.Y = pos.Y * gSet.LogFont.lfHeight;
        if (pos.X && ConCharX[CurChar-1])
            pix.X = ConCharX[CurChar-1];
    }

    if (vis)
    {
        if (gSet.isCursorColor)
        {
            SetTextColor(hDC, Cursor.foreColor);
            SetBkColor(hDC, Cursor.bgColor);
        }
        else
        {
            SetTextColor(hDC, Cursor.foreColor);
            SetBkColor(hDC, Cursor.foreColorNum < 5 ? gSet.Colors[15] : gSet.Colors[0]);
        }
    }
    else
    {
        if (drawImage && (Cursor.foreColorNum < 2) && prcLast)
            BlitPictureTo(prcLast->left, prcLast->top, prcLast->right-prcLast->left, prcLast->bottom-prcLast->top);

        SetTextColor(hDC, Cursor.bgColor);
        SetBkColor(hDC, Cursor.foreColor);
        dwSize = 99;
    }

    RECT rect;
    
    if (prcLast) {
        rect = *prcLast;
    } else
    if (!gSet.isCursorV)
    {
        if (gSet.isProportional) {
            rect.left = pix.X; /*Cursor.x * gSet.LogFont.lfWidth;*/
            rect.right = pix.X + gSet.LogFont.lfWidth; /*(Cursor.x+1) * gSet.LogFont.lfWidth;*/ //TODO: а ведь позиция следующего символа известна!
        } else {
            rect.left = pos.X * gSet.LogFont.lfWidth;
            rect.right = (pos.X+1) * gSet.LogFont.lfWidth;
        }
        //rect.top = (Cursor.y+1) * gSet.LogFont.lfHeight - MulDiv(gSet.LogFont.lfHeight, cinf.dwSize, 100);
        rect.bottom = (pos.Y+1) * gSet.LogFont.lfHeight;
        rect.top = (pos.Y * gSet.LogFont.lfHeight) /*+ 1*/;
        //if (cinf.dwSize<50)
        int nHeight = 0;
        if (dwSize) {
            nHeight = MulDiv(gSet.LogFont.lfHeight, dwSize, 100);
            if (nHeight < HCURSORHEIGHT) nHeight = HCURSORHEIGHT;
        }
        //if (nHeight < HCURSORHEIGHT) nHeight = HCURSORHEIGHT;
        rect.top = max(rect.top, (rect.bottom-nHeight));
    }
    else
    {
        if (gSet.isProportional) {
            rect.left = pix.X; /*Cursor.x * gSet.LogFont.lfWidth;*/
            //rect.right = rect.left/*Cursor.x * gSet.LogFont.lfWidth*/ //TODO: а ведь позиция следующего символа известна!
            //  + klMax(1, MulDiv(gSet.LogFont.lfWidth, cinf.dwSize, 100) 
            //  + (cinf.dwSize > 10 ? 1 : 0));
        } else {
            rect.left = pos.X * gSet.LogFont.lfWidth;
            //rect.right = Cursor.x * gSet.LogFont.lfWidth
            //  + klMax(1, MulDiv(gSet.LogFont.lfWidth, cinf.dwSize, 100) 
            //  + (cinf.dwSize > 10 ? 1 : 0));
        }
        rect.top = pos.Y * gSet.LogFont.lfHeight;
        int nR = (gSet.isProportional && ConCharX[CurChar]) // правая граница
            ? ConCharX[CurChar] : ((pos.X+1) * gSet.LogFont.lfWidth);
        //if (cinf.dwSize>=50)
        //  rect.right = nR;
        //else
        //  rect.right = min(nR, (rect.left+VCURSORWIDTH));
        int nWidth = 0;
        if (dwSize) {
            nWidth = MulDiv((nR - rect.left), dwSize, 100);
            if (nWidth < VCURSORWIDTH) nWidth = VCURSORWIDTH;
        }
        rect.right = min(nR, (rect.left+nWidth));
        //rect.right = rect.left/*Cursor.x * gSet.LogFont.lfWidth*/ //TODO: а ведь позиция следующего символа известна!
        //      + klMax(1, MulDiv(gSet.LogFont.lfWidth, cinf.dwSize, 100) 
        //      + (cinf.dwSize > 10 ? 1 : 0));
        rect.bottom = (pos.Y+1) * gSet.LogFont.lfHeight;
    }
    
    if (!prcLast) Cursor.lastRect = rect;

    if (gSet.LogFont.lfCharSet == OEM_CHARSET && !isCharBorder(Cursor.ch[0]))
    {
        if (gSet.isFixFarBorders)
            SelectFont(gSet.mh_Font);

        char tmp[2];
        WideCharToMultiByte(CP_OEMCP, 0, Cursor.ch, 1, tmp, 1, 0, 0);
        ExtTextOutA(hDC, pix.X, pix.Y,
            ETO_CLIPPED | ((drawImage && (Cursor.foreColorNum < 2) &&
            !vis) ? 0 : ETO_OPAQUE),&rect, tmp, 1, 0);
    }
    else
    {
        if (gSet.isFixFarBorders && isCharBorder(Cursor.ch[0]))
            SelectFont(gSet.mh_Font2);
        else
            SelectFont(gSet.mh_Font);

        ExtTextOut(hDC, pix.X, pix.Y,
            ETO_CLIPPED | ((drawImage && (Cursor.foreColorNum < 2) &&
            !vis) ? 0 : ETO_OPAQUE),&rect, Cursor.ch, 1, 0);
    }
}

void CVirtualConsole::UpdateCursor(bool& lRes)
{
    //------------------------------------------------------------------------
    ///| Drawing cursor |/////////////////////////////////////////////////////
    //------------------------------------------------------------------------
    Cursor.isVisiblePrevFromInfo = cinf.bVisible;

    BOOL lbUpdateTick = FALSE;
    if ((Cursor.x != csbi.dwCursorPosition.X) || (Cursor.y != csbi.dwCursorPosition.Y)) 
    {
		Cursor.isVisible = gConEmu.isActive(this) && gConEmu.isMeForeground();
        if (Cursor.isVisible) lRes = true; //force, pos changed
        lbUpdateTick = TRUE;
    } else
    if ((GetTickCount() - Cursor.nLastBlink) > Cursor.nBlinkTime)
    {
        Cursor.isVisible = gConEmu.isActive(this) && gConEmu.isMeForeground() && !Cursor.isVisible;
        lbUpdateTick = TRUE;
    }

    if ((lRes || Cursor.isVisible != Cursor.isVisiblePrev) && cinf.bVisible && isCursorValid)
    {
        lRes = true;

        SelectFont(gSet.mh_Font);

        if ((Cursor.x != csbi.dwCursorPosition.X || Cursor.y != csbi.dwCursorPosition.Y))
        {
            Cursor.isVisible = gConEmu.isActive(this) && gConEmu.isMeForeground();
            //gConEmu.cBlinkNext = 0;
        }

        ///++++
        if (Cursor.isVisiblePrev) {
            UpdateCursorDraw(MakeCoord(Cursor.x, Cursor.y), false, Cursor.lastSize, &Cursor.lastRect);
        }

        int CurChar = csbi.dwCursorPosition.Y * TextWidth + csbi.dwCursorPosition.X;
        Cursor.ch[1] = 0;
        GetCharAttr(ConChar[CurChar], ConAttr[CurChar], Cursor.ch[0], Cursor.bgColorNum, Cursor.foreColorNum);
        Cursor.foreColor = gSet.Colors[Cursor.foreColorNum];
        Cursor.bgColor = gSet.Colors[Cursor.bgColorNum];

        UpdateCursorDraw(csbi.dwCursorPosition, Cursor.isVisible, cinf.dwSize);

        Cursor.isVisiblePrev = Cursor.isVisible;

		// update cursor anyway to avoid redundant updates
		Cursor.x = csbi.dwCursorPosition.X;
		Cursor.y = csbi.dwCursorPosition.Y;
    }

	// Переместил вверх, выполнять только если старый курсор был спрятан
    // update cursor anyway to avoid redundant updates
    //Cursor.x = csbi.dwCursorPosition.X;
    //Cursor.y = csbi.dwCursorPosition.Y;

    if (lbUpdateTick)
        Cursor.nLastBlink = GetTickCount();
}


LPVOID CVirtualConsole::Alloc(size_t nCount, size_t nSize)
{
    #ifdef _DEBUG
    //HeapValidate(mh_Heap, 0, NULL);
    #endif
    size_t nWhole = nCount * nSize;
    LPVOID ptr = HeapAlloc ( mh_Heap, HEAP_GENERATE_EXCEPTIONS|HEAP_ZERO_MEMORY, nWhole );
    #ifdef _DEBUG
    //HeapValidate(mh_Heap, 0, NULL);
    #endif
    return ptr;
}

void CVirtualConsole::Free(LPVOID ptr)
{
    if (ptr && mh_Heap) {
        #ifdef _DEBUG
        //HeapValidate(mh_Heap, 0, NULL);
        #endif
        HeapFree ( mh_Heap, 0, ptr );
        #ifdef _DEBUG
        //HeapValidate(mh_Heap, 0, NULL);
        #endif
    }
}


void CVirtualConsole::Paint()
{
    if (!ghWndDC)
        return;
    
    if (!this) {
        // Залить цветом 0
        #ifdef _DEBUG
            int nBackColorIdx = 2;
        #else
            int nBackColorIdx = 0;
        #endif
        HBRUSH hBr = CreateSolidBrush(gSet.Colors[nBackColorIdx]);
        RECT rcClient; GetClientRect(ghWndDC, &rcClient);
        PAINTSTRUCT ps;
        HDC hDc = BeginPaint(ghWndDC, &ps);
        FillRect(hDc, &rcClient, hBr);
        DeleteObject(hBr);
        EndPaint(ghWndDC, &ps);
        return;
    }

    BOOL lbExcept = FALSE;
    RECT client;
    PAINTSTRUCT ps;
    HDC hPaintDc = NULL;

    
    //CSection S(&csCON, &ncsTCON);
    //if (!S.isLocked()) return; // не удалось получить доступ к CS

    GetClientRect(ghWndDC, &client);

    if (!gConEmu.isNtvdm()) {
        // после глюков с ресайзом могут быть проблемы с размером окна DC
        if (client.right < (LONG)Width || client.bottom < (LONG)Height)
            gConEmu.OnSize();
    }
    
    CSection S(&csDC, &ncsTDC);
    try {
        hPaintDc = BeginPaint(ghWndDC, &ps);

        HBRUSH hBr = NULL;
        if (((ULONG)client.right) > Width) {
            if (!hBr) hBr = CreateSolidBrush(gSet.Colors[mn_BackColorIdx]);
            RECT rcFill = MakeRect(Width, 0, client.right, client.bottom);
            FillRect(hPaintDc, &rcFill, hBr);
            client.right = Width;
        }
        if (((ULONG)client.bottom) > Height) {
            if (!hBr) hBr = CreateSolidBrush(gSet.Colors[mn_BackColorIdx]);
            RECT rcFill = MakeRect(0, Height, client.right, client.bottom);
            FillRect(hPaintDc, &rcFill, hBr);
            client.bottom = Height;
        }
        if (hBr) { DeleteObject(hBr); hBr = NULL; }


        if (!gbNoDblBuffer) {
            // Обычный режим
            BitBlt(hPaintDc, 0, 0, client.right, client.bottom, hDC, 0, 0, SRCCOPY);
        } else {
            GdiSetBatchLimit(1); // отключить буферизацию вывода для текущей нити

            GdiFlush();
            // Рисуем сразу на канвасе, без буферизации
            Update(true, &hPaintDc);
        }

        if (gbNoDblBuffer) GdiSetBatchLimit(0); // вернуть стандартный режим
    } catch(...) {
        lbExcept = TRUE;
    }
    
    S.Leave();
    
    if (lbExcept)
        Box(_T("Exception triggered in CVirtualConsole::Paint"));

    if (hPaintDc && ghWndDC) {
        EndPaint(ghWndDC, &ps);
    }
}

void CVirtualConsole::UpdateInfo()
{
    if (!ghOpWnd || !this)
        return;

    if (!gConEmu.isMainThread()) {
        return;
    }

    TCHAR szSize[128];
    wsprintf(szSize, _T("%ix%i"), mp_RCon->TextWidth(), mp_RCon->TextHeight());
    SetDlgItemText(gSet.hInfo, tConSizeChr, szSize);
    wsprintf(szSize, _T("%ix%i"), Width, Height);
    SetDlgItemText(gSet.hInfo, tConSizePix, szSize);

    wsprintf(szSize, _T("(%i, %i)-(%i, %i), %ix%i"), mr_LeftPanel.left+1, mr_LeftPanel.top+1, mr_LeftPanel.right+1, mr_LeftPanel.bottom+1, mr_LeftPanel.right-mr_LeftPanel.left+1, mr_LeftPanel.bottom-mr_LeftPanel.top+1);
    SetDlgItemText(gSet.hInfo, tPanelLeft, szSize);
    wsprintf(szSize, _T("(%i, %i)-(%i, %i), %ix%i"), mr_RightPanel.left+1, mr_RightPanel.top+1, mr_RightPanel.right+1, mr_RightPanel.bottom+1, mr_RightPanel.right-mr_RightPanel.left+1, mr_RightPanel.bottom-mr_RightPanel.top+1);
    SetDlgItemText(gSet.hInfo, tPanelRight, szSize);
}

void CVirtualConsole::Box(LPCTSTR szText)
{
#ifdef _DEBUG
    _ASSERT(FALSE);
#endif
    MessageBox(NULL, szText, _T("ConEmu"), MB_ICONSTOP);
}

RECT CVirtualConsole::GetRect()
{
	RECT rc;
	if (Width == 0 || Height == 0) { // если консоль еще не загрузилась - прикидываем по размеру GUI окна
		//rc = MakeRect(winSize.X, winSize.Y);
	    RECT rcWnd; GetClientRect(ghWnd, &rcWnd);
		RECT rcCon = gConEmu.CalcRect(CER_CONSOLE, rcWnd, CER_MAINCLIENT);
		RECT rcDC = gConEmu.CalcRect(CER_DC, rcCon, CER_CONSOLE);
		rc = MakeRect(rcDC.right, rcDC.bottom);
	} else {
		rc = MakeRect(Width, Height);
	}
	return rc;
}

void CVirtualConsole::OnFontChanged()
{
	if (!this) return;
	mb_RequiredForceUpdate = true;
}
