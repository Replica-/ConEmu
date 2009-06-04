
// WARNING!!! Содержит юникодные символы !!!

#include "Header.h"
#include <Tlhelp32.h>

WARNING("При быстром наборе текста курсор часто 'замерзает' на одной из букв, но продолжает двигаться дальше");

WARNING("Глюк с частичной отрисовкой экрана часто появляется при Alt-F7, Enter. Особенно, если файла нет");
// Иногда не отрисовывается диалог поиска полностью - только бежит текущая сканируемая директория.
// Иногда диалог отрисовался, но часть до текста "..." отсутствует

//PRAGMA_ERROR("При попытке компиляции F:\\VCProject\\FarPlugin\\PPCReg\\compile.cmd - Enter в консоль не прошел");

TODO("CES_CONMANACTIVE deprecated");

WARNING("Может быть хватит ServerThread? А MonitorThread зарезервировать только для запуска процесса, и сразу из него выйти");

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

#define Free SafeFree
#define Alloc calloc

#define Assert(V) if ((V)==FALSE) { TCHAR szAMsg[MAX_PATH*2]; wsprintf(szAMsg, _T("Assertion (%s) at\n%s:%i"), _T(#V), _T(__FILE__), __LINE__); Box(szAMsg); }

#ifdef _DEBUG
#define HEAPVAL MCHKHEAP
#else
#define HEAPVAL 
#endif

CRealConsole::CRealConsole(CVirtualConsole* apVCon)
{
	mp_VCon = apVCon;
    memset(Title,0,sizeof(Title)); memset(TitleCmp,0,sizeof(TitleCmp));
	mp_tabs = NULL; mn_tabsCount = 0; ms_PanelTitle[0] = 0; mn_ActiveTab = 0;

	wcscpy(Title, L"ConEmu");
	wcscpy(ms_PanelTitle, Title);

    mh_MonitorThread = NULL; mn_MonitorThreadID = 0; mn_ConEmuC_PID = 0; mh_ConEmuC = NULL; mh_ConEmuCInput = NULL;

    ms_ConEmuC_Pipe[0] = 0; ms_ConEmuCInput_Pipe[0] = 0; ms_VConServer_Pipe[0] = 0;
    mh_TermEvent = CreateEvent(NULL,TRUE/*MANUAL - используется в нескольких нитях!*/,FALSE,NULL); ResetEvent(mh_TermEvent);
    mh_ForceReadEvent = CreateEvent(NULL,FALSE,FALSE,NULL);
    mh_EndUpdateEvent = CreateEvent(NULL,FALSE,FALSE,NULL);
	WARNING("mh_Sync2WindowEvent убрать");
    mh_Sync2WindowEvent = CreateEvent(NULL,FALSE,FALSE,NULL);
    mh_ConChanged = CreateEvent(NULL,FALSE,FALSE,NULL);
    mh_CursorChanged = NULL; mb_Detached = FALSE;
    mb_FullRetrieveNeeded = FALSE;

    mn_ActiveStatus = 0;
    isShowConsole = false;
    mb_ConsoleSelectMode = false;
    mn_ProcessCount = 0; mn_FarPID = 0; mn_InRecreate = 0; mb_ProcessRestarted = FALSE;
    mh_ServerSemaphore = NULL;
    memset(mh_ServerThreads, 0, sizeof(mh_ServerThreads)); mh_ActiveServerThread = NULL;
    memset(mn_ServerThreadsId, 0, sizeof(mn_ServerThreadsId));

    memset(&con, 0, sizeof(con)); //WARNING! Содержит CriticalSection, поэтому сброс выполнять ПЕРЕД InitializeCriticalSection(&con.cs);


    InitializeCriticalSection(&csPRC); ncsTPRC = 0;
    InitializeCriticalSection(&con.cs); con.ncsT = 0;


    hConWnd = NULL; mh_GuiAttached = NULL; mn_Focused = -1;
    mn_LastVKeyPressed = 0;
    mn_LastConFullReadTick = 0; mn_LastConRgnReadTick = 0;
	mh_LogInput = NULL; mpsz_LogInputFile = NULL; mpsz_LogPackets = NULL; mn_LogPackets = 0;

	mb_UseLogs = gSet.isAdvLogging && gSet.szDumpPackets[0]!=0;

	SetTabs(NULL,1); // Для начала - показывать вкладку Console, а там ФАР разберется
	
	PreInit(FALSE); // просто инициализировать переменные размеров...
}

CRealConsole::~CRealConsole()
{
    StopThread();
    
    MCHKHEAP

    if (con.pConChar)
        { Free(con.pConChar); con.pConChar = NULL; }
    if (con.pConAttr)
        { Free(con.pConAttr); con.pConAttr = NULL; }



    SafeCloseHandle(mh_ConEmuC); mn_ConEmuC_PID = 0;
    SafeCloseHandle(mh_ConEmuCInput);
    
    SafeCloseHandle(mh_ServerSemaphore);
    SafeCloseHandle(mh_GuiAttached);

    DeleteCriticalSection(&csPRC);
    DeleteCriticalSection(&con.cs);
    
    
    if (mp_tabs) Free(mp_tabs);
    mp_tabs = NULL; mn_tabsCount = 0; mn_ActiveTab = 0;

	// 
	CloseLogFiles();
}

BOOL CRealConsole::PreCreate(BOOL abDetached)
{
    if (abDetached) {
        // Пока ничего не делаем - просто создается серверная нить
        if (!PreInit()) { //TODO: вообще-то PreInit() уже наверное вызван...
            return FALSE;
        }
        mb_Detached = TRUE;
    } else
    if (gSet.nAttachPID) {
        // Attach - only once
        DWORD dwPID = gSet.nAttachPID; gSet.nAttachPID = 0;
        if (!AttachPID(dwPID)) {
            return FALSE;
        }
    } else {
        if (!StartProcess()) {
            return FALSE;
        }
        // Вроде бы с запуском через нить клики мышкой в консоль начинают ходить сразу, но
        // часто окно конэму вообще не активируется
    }

	if (!StartMonitorThread()) {
        return FALSE;
	}

	return TRUE;
}


void CRealConsole::DumpConsole(HANDLE ahFile)
{
	DWORD dw = 0;
    if (con.pConChar && con.pConAttr) {
        WriteFile(ahFile, con.pConChar, con.nTextWidth * con.nTextHeight * 2, &dw, NULL);
        WriteFile(ahFile, con.pConAttr, con.nTextWidth * con.nTextHeight * 2, &dw, NULL);
    }
}

BOOL CRealConsole::SetConsoleSize(COORD size)
{
    _ASSERTE(hConWnd && ms_ConEmuC_Pipe[0]);

    if (!hConWnd || ms_ConEmuC_Pipe[0] == 0) {
        Box(_T("Console was not created (CRealConsole::SetConsoleSize)"));
        return false; // консоль пока не создана?
    }
    
    _ASSERTE(size.X>MIN_CON_WIDTH && size.Y>MIN_CON_HEIGHT);
    
    if (size.X</*4*/MIN_CON_WIDTH)
        size.X = /*4*/MIN_CON_WIDTH;
    if (size.Y</*3*/MIN_CON_HEIGHT)
        size.Y = /*3*/MIN_CON_HEIGHT;

    bool lbRc = false;
    BOOL fSuccess = FALSE;
    DWORD dwRead = 0;
    int nInSize = sizeof(CESERVER_REQ)+sizeof(USHORT)+sizeof(COORD)+sizeof(SHORT)+sizeof(SMALL_RECT)-1;
    CESERVER_REQ *pIn = (CESERVER_REQ*)calloc(nInSize,1);
    _ASSERTE(pIn);
    if (!pIn) return false;
    pIn->nSize = nInSize;
    int nOutSize = sizeof(CESERVER_REQ)+sizeof(CONSOLE_SCREEN_BUFFER_INFO)-1;
    CESERVER_REQ *pOut = (CESERVER_REQ*)calloc(nOutSize,1);
    _ASSERTE(pOut);
    if (!pOut) { free(pIn); return false; }
    pOut->nSize = nOutSize;

    pIn->nVersion = CESERVER_REQ_VER;
    pIn->nCmd = CECMD_SETSIZE;

	// Устанавливаем параметры для передачи в ConEmuC
	*((USHORT*)pIn->Data) = con.bBufferHeight ? gSet.DefaultBufferHeight : 0;
    memmove(pIn->Data + sizeof(USHORT), &size, sizeof(COORD));
	SHORT nSendTopLine = (gSet.AutoScroll || !con.bBufferHeight) ? -1 : con.nTopVisibleLine;
	memmove(pIn->Data + sizeof(USHORT)+sizeof(COORD), &nSendTopLine, sizeof(SHORT));

	// Для режима BufferHeight нужно передать еще и видимый прямоугольник (нужна только верхняя координата?)
    if (con.bBufferHeight) {
        // global flag of the first call which is:
        // *) after getting all the settings
        // *) before running the command
        static bool s_isFirstCall = true;

        // case: buffer mode: change buffer
        CONSOLE_SCREEN_BUFFER_INFO sbi = con.m_sbi;

        sbi.dwSize.X = size.X; // new
        if (s_isFirstCall)
        {
            // first call: buffer height = from settings
            s_isFirstCall = false;
            sbi.dwSize.Y = max(gSet.DefaultBufferHeight, size.Y);
        }
        else
        {
            if (sbi.dwSize.Y == sbi.srWindow.Bottom - sbi.srWindow.Top + 1)
                // no y-scroll: buffer height = new window height
                sbi.dwSize.Y = size.Y;
            else
                // y-scroll: buffer height = old buffer height
                sbi.dwSize.Y = max(sbi.dwSize.Y, size.Y);
        }

        SMALL_RECT rect;
        rect.Top = sbi.srWindow.Top;
        rect.Left = sbi.srWindow.Left;
        rect.Right = rect.Left + size.X - 1;
        rect.Bottom = rect.Top + size.Y - 1;
        if (rect.Right >= sbi.dwSize.X)
        {
            int shift = sbi.dwSize.X - 1 - rect.Right;
            rect.Left += shift;
            rect.Right += shift;
        }
        if (rect.Bottom >= sbi.dwSize.Y)
        {
            int shift = sbi.dwSize.Y - 1 - rect.Bottom;
            rect.Top += shift;
            rect.Bottom += shift;
        }
        
        memmove(pIn->Data + sizeof(USHORT) + sizeof(COORD) + sizeof(SHORT), &rect, sizeof(rect));
    }

    fSuccess = CallNamedPipe(ms_ConEmuC_Pipe, pIn, pIn->nSize, pOut, pOut->nSize, &dwRead, 500);

    if (fSuccess && dwRead>=(DWORD)nOutSize) {
        if (pOut->nCmd == pIn->nCmd) {
            CONSOLE_SCREEN_BUFFER_INFO sbi = {{0,0}};
            memmove(&sbi, pOut->Data, sizeof(sbi));
            if (sbi.dwSize.X == size.X && sbi.dwSize.Y == size.Y) {
                con.m_sbi = sbi;
                if (sbi.dwSize.X == con.m_sbi.dwSize.X && sbi.dwSize.Y == con.m_sbi.dwSize.Y) {
                    SetEvent(mh_ConChanged);
                }
                lbRc = true;
            }
        }
    }

    return lbRc;
}

// Изменить размер консоли по размеру окна (главного)
void CRealConsole::SyncConsole2Window()
{
    if (!this)
        return;

    if (GetCurrentThreadId() != mn_MonitorThreadID) {
        RECT rcClient; GetClientRect(ghWnd, &rcClient);
		_ASSERTE(rcClient.right>250 && rcClient.bottom>200);

        // Посчитать нужный размер консоли
        RECT newCon = gConEmu.CalcRect(CER_CONSOLE, rcClient, CER_MAINCLIENT);

        if (newCon.right==TextWidth() && newCon.bottom==TextHeight())
            return; // размеры не менялись

        SetEvent(mh_Sync2WindowEvent);
        return;
    }

    DEBUGLOGFILE("SyncConsoleToWindow\n");

    RECT rcClient; GetClientRect(ghWnd, &rcClient);
	_ASSERTE(rcClient.right>250 && rcClient.bottom>200);

    // Посчитать нужный размер консоли
    RECT newCon = gConEmu.CalcRect(CER_CONSOLE, rcClient, CER_MAINCLIENT);

    SetConsoleSize(MakeCoord(newCon.right, newCon.bottom));
}

BOOL CRealConsole::AttachConemuC(HWND ahConWnd, DWORD anConemuC_PID)
{
    DWORD dwErr = 0;
    HANDLE hProcess = NULL;

    hProcess = OpenProcess(PROCESS_QUERY_INFORMATION|SYNCHRONIZE, FALSE, anConemuC_PID);
    if (!hProcess) {
        DisplayLastError(L"Can't open ConEmuC process! Attach is impossible!", dwErr = GetLastError());
        return FALSE;
    }

    // Событие "изменения" консоли //2009-05-14 Теперь события обрабатываются в GUI, но прийти из консоли может изменение размера курсора
    wsprintfW(ms_ConEmuC_Pipe, CE_CURSORUPDATE, mn_ConEmuC_PID);
    mh_CursorChanged = CreateEvent ( NULL, FALSE, FALSE, ms_ConEmuC_Pipe );
    if (!mh_CursorChanged) {
        ms_ConEmuC_Pipe[0] = 0;
        DisplayLastError(L"Can't create event!");
        return FALSE;
    }

    SetHwnd(ahConWnd);
    ProcessAdd(anConemuC_PID);

    mh_ConEmuC = hProcess;
    mn_ConEmuC_PID = anConemuC_PID;

	CreateLogFiles();

    // Имя пайпа для управления ConEmuC
    wsprintfW(ms_ConEmuC_Pipe, CESERVERPIPENAME, L".", mn_ConEmuC_PID);
    wsprintfW(ms_ConEmuCInput_Pipe, CESERVERINPUTNAME, L".", mn_ConEmuC_PID);
    MCHKHEAP

    //SetConsoleSize(MakeCoord(TextWidth,TextHeight));
	RECT rcWnd; GetClientRect(ghWnd, &rcWnd);
	RECT rcCon = gConEmu.CalcRect(CER_CONSOLE, rcWnd, CER_MAINCLIENT);
	SetConsoleSize(MakeCoord(rcCon.right,rcCon.bottom));

    // Передернуть нить VCon
    SetEvent(mh_ConChanged);

    return TRUE;
}

BOOL CRealConsole::AttachPID(DWORD dwPID)
{
    TODO("AttachPID пока работать не будет");
    return FALSE;

#ifdef ALLOW_ATTACHPID
    #ifdef MSGLOGGER
        TCHAR szMsg[100]; wsprintf(szMsg, _T("Attach to process %i"), (int)dwPID);
        DEBUGSTR(szMsg);
    #endif
    BOOL lbRc = AttachConsole(dwPID);
    if (!lbRc) {
        DEBUGSTR(_T(" - failed\n"));
        BOOL lbFailed = TRUE;
        DWORD dwErr = GetLastError();
        if (/*dwErr==0x1F || dwErr==6 &&*/ dwPID == -1)
        {
            // Если ConEmu запускается из FAR'а батником - то родительский процесс - CMD.EXE, а он уже скорее всего закрыт. то есть подцепиться не удастся
            HWND hConsole = FindWindowEx(NULL,NULL,_T("ConsoleWindowClass"),NULL);
            if (hConsole && IsWindowVisible(hConsole)) {
                DWORD dwCurPID = 0;
                if (GetWindowThreadProcessId(hConsole,  &dwCurPID)) {
                    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS,FALSE,dwCurPID);
                    dwErr = GetLastError();
                    if (AttachConsole(dwCurPID))
                        lbFailed = FALSE;
                    else
                        dwErr = GetLastError();
                }
            }
        }
        if (lbFailed) {
            TCHAR szErr[255];
            wsprintf(szErr, _T("AttachConsole failed (PID=%i)!"), dwPID);
            DisplayLastError(szErr, dwErr);
            return FALSE;
        }
    }
    DEBUGSTR(_T(" - OK"));

    TODO("InitHandler в GUI наверное уже и не нужен...");
    //InitHandlers(FALSE);

    // Попытаться дернуть плагин для установки шрифта.
    CConEmuPipe pipe;
    
    //DEBUGSTR(_T("CheckProcesses\n"));
    //gConEmu.CheckProcesses(0,TRUE);
    
    if (pipe.Init(_T("DefFont.in.attach"), TRUE))
        pipe.Execute(CMD_DEFFONT);

    return TRUE;
#endif
}


DWORD CRealConsole::MonitorThread(LPVOID lpParameter)
{
    CRealConsole* pRCon = (CRealConsole*)lpParameter;
    //BOOL lbRc = pRCon->StartProcess(); -- лучше так все-таки не делать. велика вероятность блокировки при изменении TOPMOST главного окна
    
    //if (!pRCon->RetrieveConsoleInfo()) {
    //    return 1;
    //}
    
    _ASSERTE(pRCon->mh_ConChanged!=NULL);
    _ASSERTE(pRCon->mb_Detached || pRCon->mh_CursorChanged!=NULL);

    BOOL bDetached = pRCon->mb_Detached;
    //TODO: а тут мы будем читать консоль...
    #define IDEVENT_TERM  0
    #define IDEVENT_CONCHANGED 1
    #define IDEVENV_FORCEREADEVENT 2
    #define IDEVENT_SYNC2WINDOW 3
    #define IDEVENT_CURSORCHANGED 4
    #define IDEVENT_CONCLOSED 5
    #define EVENTS_COUNT (IDEVENT_CONCLOSED+1)
    HANDLE hEvents[EVENTS_COUNT];
    hEvents[IDEVENT_TERM] = pRCon->mh_TermEvent;
    hEvents[IDEVENT_CONCHANGED] = pRCon->mh_ConChanged;
    hEvents[IDEVENV_FORCEREADEVENT] = pRCon->mh_ForceReadEvent; // Использовать, чтобы вызвать Update & Invalidate
    hEvents[IDEVENT_SYNC2WINDOW] = pRCon->mh_Sync2WindowEvent;
    hEvents[IDEVENT_CURSORCHANGED] = pRCon->mh_CursorChanged;
    hEvents[IDEVENT_CONCLOSED] = pRCon->mh_ConEmuC;
    DWORD  nEvents = bDetached ? (IDEVENT_SYNC2WINDOW+1) : countof(hEvents);
    _ASSERT(EVENTS_COUNT==countof(hEvents)); // проверить размерность

    DWORD  nWait = 0;
    BOOL   bLoop = TRUE, bIconic = FALSE, bFirst = TRUE;

    DWORD nElapse = max(10,gSet.nMainTimerElapse);
    
    TODO("Нить не завершается при F10 в фаре - процессы пока не инициализированы...")
    while (TRUE/*bLoop*/)
    {
        gSet.Performance(tPerfInterval, TRUE); // именно обратный отсчет. Мы смотрим на промежуток МЕЖДУ таймерами

        if (bDetached && pRCon->mh_ConEmuC && pRCon->mh_CursorChanged) {
            bDetached = FALSE;
            hEvents[IDEVENT_CURSORCHANGED] = pRCon->mh_CursorChanged;
            hEvents[IDEVENT_CONCLOSED] = pRCon->mh_ConEmuC;
            nEvents = countof(hEvents);
        }

        bIconic = IsIconic(ghWnd);
        // в минимизированном режиме - сократить расходы
        nWait = WaitForMultipleObjects(nEvents, hEvents, FALSE, bIconic ? max(1000,nElapse) : nElapse);
        _ASSERTE(nWait!=(DWORD)-1);

        if (nWait == IDEVENT_TERM /*|| !bLoop*/ || nWait == IDEVENT_CONCLOSED)
            break; // требование завершения нити

        // Проверим, что ConEmuC жив
        if (pRCon->mh_ConEmuC) {
            DWORD dwExitCode = 0;
            BOOL fSuccess = GetExitCodeProcess(pRCon->mh_ConEmuC, &dwExitCode);
            if (dwExitCode!=STILL_ACTIVE) {
                pRCon->StopSignal();
                return 0;
            }
        }

        // Если консоль не должна быть показана - но ее кто-то отобразил 
        if (!pRCon->isShowConsole && !gSet.isConVisible)
        {
            /*if (foreWnd == hConWnd)
                SetForegroundWindow(ghWnd);*/
            if (IsWindowVisible(pRCon->hConWnd))
                ShowWindow(pRCon->hConWnd, SW_HIDE);
        }

        // Размер консоли меняем в том треде, в котором это требуется. Иначе можем заблокироваться при Update (InitDC)
        // Требуется изменение размеров консоли
        /*if (nWait == (IDEVENT_SYNC2WINDOW)) {
            pRCon->SetConsoleSize(pRCon->m_ReqSetSize);
            //SetEvent(pRCon->mh_ReqSetSizeEnd);
            //continue; -- и сразу обновим информацию о ней
        }*/
        
        bool bActive = gConEmu.isActive(pRCon->mp_VCon);

        DWORD dwT1 = GetTickCount();

        try {   
            ResetEvent(pRCon->mh_EndUpdateEvent);
            
            if (!bDetached && (bFirst || (nWait==IDEVENT_CONCHANGED) || (nWait==IDEVENT_CURSORCHANGED))) {
                bFirst = FALSE;
                if (!pRCon->RetrieveConsoleInfo((nWait==IDEVENT_CURSORCHANGED))) {
                    _ASSERT(FALSE);
                }
            }

            if (nWait == IDEVENT_SYNC2WINDOW && pRCon->hConWnd) {
                pRCon->SyncConsole2Window();
            }

            if (pRCon->hConWnd 
                && GetWindowText(pRCon->hConWnd, pRCon->TitleCmp, countof(pRCon->TitleCmp)-2)
                && wcscmp(pRCon->Title, pRCon->TitleCmp))
            {
                wcscpy(pRCon->Title, pRCon->TitleCmp);
				// иначе может среагировать на изменение заголовка ДО того,
				// как мы узнаем, что активировался редактор...
				TODO("Должно срабатывать и при запуске консольной программы!");
				if (pRCon->Title[0] == L'{' || pRCon->Title[0] == L'(')
					pRCon->CheckPanelTitle();

                if (bActive)
                    gConEmu.UpdateTitle(pRCon->TitleCmp);
				TabBar.Update(); // сменить заголовок закладки?
			} else if (bActive) {
				if (wcscmp(pRCon->Title, gConEmu.GetTitle()))
					gConEmu.UpdateTitle(pRCon->TitleCmp);
			}

			bool lbForceUpdate = pRCon->CheckBufferSize();
			if (lbForceUpdate && gConEmu.isActive(pRCon->mp_VCon)) // размер текущего консольного окна был изменен
                PostMessage(ghWnd, WM_TIMER, 0, 0); // послать в главную нить запрос на обновление размера
            if (!lbForceUpdate && (nWait == (WAIT_OBJECT_0+1)))
                lbForceUpdate = true;

            // 04.06.2009 Maks - видимо, EndUpdateEvent не всегда вызывался!
            if (pRCon->mp_VCon->Update(lbForceUpdate) && bActive) {
	            // 04.06.2009 Maks - если консоль активна - Invalidate вызовер сам VCon при необходимости
                //gConEmu.m_Child.Invalidate();
                //InvalidateRect(ghWndDC, NULL, FALSE);
            }
            
            SetEvent(pRCon->mh_EndUpdateEvent);
        } catch(...) {
            bLoop = FALSE;
        }

        /*if (nWait == (WAIT_OBJECT_0+2)) {
            SetEvent(pRCon->mh_ReqSetSizeEnd);
        }*/

        DWORD dwT2 = GetTickCount();
        DWORD dwD = max(10,(dwT2 - dwT1));
        nElapse = nElapse*0.7 + dwD*0.3;

        if (!bLoop) {
            #ifdef _DEBUG
            _ASSERT(FALSE);
            #endif
            pRCon->Box(_T("Exception triggered in CRealConsole::MonitorThread"));
            bLoop = true;
        }

        gSet.Performance(tPerfInterval, FALSE);
    }
   
    // Завершение серверных нитей этой консоли
	OutputDebugString(L"About to terminate main server thread (MonitorThread)\n");
    if (pRCon->ms_VConServer_Pipe[0]) // значит хотя бы одна нить была запущена
    {   
        pRCon->StopSignal(); // уже должен быть выставлен, но на всякий случай
        //
        HANDLE hPipe = INVALID_HANDLE_VALUE;
		DWORD dwWait = 0;
        // Передернуть пайпы, чтобы нити сервера завершились
        for (int i=0; i<MAX_SERVER_THREADS; i++) {
			OutputDebugString(L"Touching our server pipe\n");
			HANDLE hServer = pRCon->mh_ActiveServerThread;
            hPipe = CreateFile(pRCon->ms_VConServer_Pipe,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
			if (hPipe == INVALID_HANDLE_VALUE) {
				OutputDebugString(L"All pipe instances closed?\n");
                break;
			}
			OutputDebugString(L"Waiting server pipe thread\n");
			dwWait = WaitForSingleObject(hServer, 200); // пытаемся дождаться, пока нить завершится
            // Просто закроем пайп - его нужно было передернуть
            CloseHandle(hPipe);
            hPipe = INVALID_HANDLE_VALUE;
        }
        // Немного подождем, пока ВСЕ нити завершатся
		OutputDebugString(L"Checking server pipe threads are closed\n");
        WaitForMultipleObjects(MAX_SERVER_THREADS, pRCon->mh_ServerThreads, TRUE, 500);
        for (int i=0; i<MAX_SERVER_THREADS; i++) {
			if (WaitForSingleObject(pRCon->mh_ServerThreads[i],0) != WAIT_OBJECT_0) {
				OutputDebugString(L"### Terminating mh_ServerThreads\n");
                TerminateThread(pRCon->mh_ServerThreads[i],0);
			}
            CloseHandle(pRCon->mh_ServerThreads[i]);
            pRCon->mh_ServerThreads[i] = NULL;
        }
    }
    
    // Finalize
    //SafeCloseHandle(pRCon->mh_MonitorThread);

	OutputDebugString(L"Leaving MonitorThread\n");
    
    return 0;
}

BOOL CRealConsole::PreInit(BOOL abCreateBuffers/*=TRUE*/)
{
    // Инициализировать переменные m_sbi, m_ci, m_sel
    RECT rcWnd; GetClientRect(ghWnd, &rcWnd);
	// isBufferHeight использовать нельзя, т.к. con.m_sbi еще не инициализирован!
	if (gSet.ForceBufferHeight && gSet.DefaultBufferHeight && !con.bBufferHeight)
		con.bBufferHeight = TRUE;

	if (con.bBufferHeight) {
		// скорректировать ширину окна на ширину появляющейся полосы прокрутки
		if (!gConEmu.ActiveCon()->RCon()->isBufferHeight())
			rcWnd.right -= GetSystemMetrics(SM_CXVSCROLL);
	} else {
		// // скорректировать ширину окна на ширину убирающейся полосы прокрутки
		if (gConEmu.ActiveCon()->RCon()->isBufferHeight())
			rcWnd.right += GetSystemMetrics(SM_CXVSCROLL);
	}
    RECT rcCon = gConEmu.CalcRect(CER_CONSOLE, rcWnd, CER_MAINCLIENT);
    _ASSERTE(rcCon.right!=0 && rcCon.bottom!=0);
	if (con.bBufferHeight) {
		con.m_sbi.dwSize = MakeCoord(rcCon.right,gSet.DefaultBufferHeight);
	} else {
		con.m_sbi.dwSize = MakeCoord(rcCon.right,rcCon.bottom);
	}
    con.m_sbi.wAttributes = 7;
    con.m_sbi.srWindow.Right = rcCon.right-1; con.m_sbi.srWindow.Bottom = rcCon.bottom-1;
    con.m_sbi.dwMaximumWindowSize = con.m_sbi.dwSize;
    con.m_ci.dwSize = 15; con.m_ci.bVisible = TRUE;
    if (!InitBuffers(0))
        return FALSE;
    return TRUE;
}

BOOL CRealConsole::StartMonitorThread()
{
	BOOL lbRc = FALSE;

	_ASSERT(mh_MonitorThread==NULL);

    mh_MonitorThread = CreateThread(NULL, 0, MonitorThread, (LPVOID)this, 0, &mn_MonitorThreadID);

    if (mh_MonitorThread == NULL) {
        DisplayLastError(_T("Can't create console thread!"));
	} else {
		lbRc = TRUE;
	}

	return lbRc;
}

BOOL CRealConsole::StartProcess()
{
    BOOL lbRc = FALSE;

    //CSection SCON(&csCON, &ncsTCON);

    if (!mb_ProcessRestarted) {
	    if (!PreInit())
	        return FALSE;
	}
	
    //if (ghConWnd) {
    //    // Сначала нужно отцепиться от текущей консоли
    //    FreeConsole(); ghConWnd = NULL;
    //}
    
    
    //ConExeProps props;
    
    // 2009-05-13 Теперь запуск консоли идет в отдельном процессе, так что ярлык не помешает
    // Если запускались с ярлыка - это нихрена не поможет... информация похоже в .lnk сохраняется...
    //RegistryProps(FALSE, props, CEC_INITTITLE);

    /*if (!isShowConsole && !gSet.isConVisible
        #ifdef MSGLOGGER
        && !IsDebuggerPresent()
        #endif
        ) SetWindowPos(ghWnd, HWND_TOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE);
    AllocConsole();

    InitHandlers(TRUE);

    if (!isShowConsole && !gSet.isConVisible
        #ifdef MSGLOGGER
        && !IsDebuggerPresent()
        #endif
        ) SetWindowPos(ghWnd, HWND_NOTOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE);

    SetForegroundWindow(ghWnd);*/
    
    //RegistryProps(TRUE, props);


    // Уже должно быть сделано в InitHandlers
    /*RECT rcWnd; GetClientRect(ghWnd, &rcWnd);
    RECT rcCon = gConEmu.CalcRect(CER_CONSOLE, rcWnd, CER_MAINCLIENT);
    SetConsoleSize(MakeCoord(rcCon.right,rcCon.bottom));*/

//#pragma message("error: почему-то консоль создалась схлопнутой по ширине/высоте, хотя ее размеры были нормальные!")
    // TODO: перед Update нужно позвать установку размера консоли!

    //Update(true);

    //SCON.Leave();

    /*if (gSet.isConMan) {
        if (!gConEmu.InitConMan(gSet.GetCmd())) {
            // Иначе жестоко получается. ConEmu вообще будет сложно запустить...
            gSet.isConMan = false;
        } else {
            //SetForegroundWindow(ghWnd);
            return TRUE;
        }
    }*/ 
    
    //if (!gSet.isConMan)
    {
        STARTUPINFO si;
        PROCESS_INFORMATION pi;

        ZeroMemory( &si, sizeof(si) );
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW|STARTF_USECOUNTCHARS|STARTF_USEPOSITION;
        si.lpTitle = CEC_INITTITLE;
        si.dwXCountChars = con.m_sbi.dwSize.X;
		si.dwYCountChars = con.m_sbi.dwSize.Y;
        si.wShowWindow = gSet.isConVisible ? SW_SHOWNORMAL : SW_HIDE;
        RECT rcDC; GetWindowRect(ghWndDC, &rcDC);
        si.dwX = rcDC.left; si.dwY = rcDC.top;
        ZeroMemory( &pi, sizeof(pi) );

        int nStep = 1;
        wchar_t* psCurCmd = NULL;
        while (nStep <= 2)
        {
            MCHKHEAP
            /*if (!*gSet.GetCmd()) {
                gSet.psCurCmd = _tcsdup(gSet.Buffer Height == 0 ? _T("far") : _T("cmd"));
                nStep ++;
            }*/

            LPTSTR lpszCmd = (LPTSTR)gSet.GetCmd();

            int nLen = _tcslen(lpszCmd);
            TCHAR *pszSlash=NULL;
            nLen += _tcslen(gConEmu.ms_ConEmuExe) + 128;
            psCurCmd = (wchar_t*)malloc(nLen*sizeof(wchar_t));
            _ASSERTE(psCurCmd);
            wcscpy(psCurCmd, L"\"");
            wcscat(psCurCmd, gConEmu.ms_ConEmuExe);
            pszSlash = wcsrchr(psCurCmd, _T('\\'));
            MCHKHEAP
            wcscpy(pszSlash+1, L"ConEmuC.exe\" ");
			int nWndWidth = con.m_sbi.dwSize.X;
			int nWndHeight = con.m_sbi.dwSize.Y;
			GetConWindowSize(con.m_sbi, nWndWidth, nWndHeight);
            wsprintf(psCurCmd+wcslen(psCurCmd), L"/BW=%i /BH=%i /BZ=%i", 
				nWndWidth, nWndHeight,
				(con.bBufferHeight ? gSet.DefaultBufferHeight : 0));
            if (gSet.isAdvLogging) wcscat(psCurCmd, L" /LOG");
            wcscat(psCurCmd, L" /CMD ");
            wcscat(psCurCmd, lpszCmd);
            MCHKHEAP

            #ifdef MSGLOGGER
            DEBUGSTR(psCurCmd);DEBUGSTR(_T("\n"));
            #endif
            try {
                lbRc = CreateProcess(NULL, psCurCmd, NULL, NULL, FALSE, 
					NORMAL_PRIORITY_CLASS|
					CREATE_DEFAULT_ERROR_MODE|CREATE_NEW_CONSOLE
					//|CREATE_NEW_PROCESS_GROUP - низя! перестает срабатывать Ctrl-C
					, NULL, NULL, &si, &pi);
                DEBUGSTR(_T("CreateProcess finished\n"));
            } catch(...) {
                lbRc = FALSE;
            }
            if (lbRc)
            {
                DEBUGSTR(_T("CreateProcess OK\n"));
                lbRc = TRUE;

                /*if (!AttachPID(pi.dwProcessId)) {
                    DEBUGSTR(_T("AttachPID failed\n"));
                    return FALSE;
                }
                DEBUGSTR(_T("AttachPID OK\n"));*/

                break; // OK, запустили
            } else {
                //Box("Cannot execute the command.");
                DWORD dwLastError = GetLastError();
                DEBUGSTR(_T("CreateProcess failed\n"));
                int nLen = _tcslen(psCurCmd);
                TCHAR* pszErr=(TCHAR*)Alloc(nLen+100,sizeof(TCHAR));
                
                if (0==FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    NULL, dwLastError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
                    pszErr, 1024, NULL))
                {
                    wsprintf(pszErr, _T("Unknown system error: 0x%x"), dwLastError);
                }
                
                nLen += _tcslen(pszErr);
                TCHAR* psz=(TCHAR*)Alloc(nLen+100,sizeof(TCHAR));
                int nButtons = MB_OK|MB_ICONEXCLAMATION|MB_SETFOREGROUND;
                
                _tcscpy(psz, _T("Cannot execute the command.\r\n"));
                _tcscat(psz, psCurCmd); _tcscat(psz, _T("\r\n"));
                _tcscat(psz, pszErr);
                if (psz[_tcslen(psz)-1]!=_T('\n')) _tcscat(psz, _T("\r\n"));
                if (!gSet.psCurCmd && StrStrI(gSet.GetCmd(), gSet.GetDefaultCmd())==NULL) {
                    _tcscat(psz, _T("\r\n\r\n"));
                    _tcscat(psz, _T("Do You want to simply start "));
					_tcscat(psz, gSet.GetDefaultCmd());
					_tcscat(psz, _T("?"));
                    nButtons |= MB_YESNO;
                }
                MCHKHEAP
                //Box(psz);
                int nBrc = MessageBox(NULL, psz, _T("ConEmu"), nButtons);
                Free(psz); Free(pszErr);
                if (nBrc!=IDYES) {
                    gConEmu.Destroy();
                    return FALSE;
                }
                // Выполнить стандартную команду...
                gSet.psCurCmd = _tcsdup(gSet.GetDefaultCmd());
                nStep ++;
                MCHKHEAP
                if (psCurCmd) free(psCurCmd); psCurCmd = NULL;
            }
        }

        MCHKHEAP
        if (psCurCmd) free(psCurCmd); psCurCmd = NULL;
        MCHKHEAP

        //TODO: а делать ли это?
        CloseHandle(pi.hThread); pi.hThread = NULL;
        //CloseHandle(pi.hProcess); pi.hProcess = NULL;
        mn_ConEmuC_PID = pi.dwProcessId;
        mh_ConEmuC = pi.hProcess; pi.hProcess = NULL;

		CreateLogFiles();
        
        // Событие "изменения" консоль //2009-05-14 Теперь события обрабатываются в GUI, но прийти из консоли может изменение размера курсора
        wsprintfW(ms_ConEmuC_Pipe, CE_CURSORUPDATE, mn_ConEmuC_PID);
        mh_CursorChanged = CreateEvent ( NULL, FALSE, FALSE, ms_ConEmuC_Pipe );
        
        // Имя пайпа для управления ConEmuC
        wsprintfW(ms_ConEmuC_Pipe, CESERVERPIPENAME, L".", mn_ConEmuC_PID);
        wsprintfW(ms_ConEmuCInput_Pipe, CESERVERINPUTNAME, L".", mn_ConEmuC_PID);
        MCHKHEAP
        
    }

    return lbRc;
}



void CRealConsole::SendMouseEvent(UINT messg, WPARAM wParam, int x, int y)
{
	#ifndef WM_MOUSEHWHEEL
	#define WM_MOUSEHWHEEL                  0x020E
	#endif

    if (!this || !hConWnd)
        return;

    //BOOL lbStdMode = FALSE;
    //if (!con.bBufferHeight)
    //    lbStdMode = TRUE;
	if (con.bBufferHeight) {
		TODO("Ручная обработка выделения, на консоль полагаться не следует...");
		if (messg == WM_MOUSEWHEEL) {
			SHORT nDir = (SHORT)HIWORD(wParam);
			if (nDir > 0) {
				OnScroll(SB_LINEUP);
				//OnScroll(SB_PAGEUP);
			} else if (nDir < 0) {
				OnScroll(SB_LINEDOWN);
				//OnScroll(SB_PAGEDOWN);
			}
		}
		return;
	}

    //if (lbStdMode)
    {
        Assert ( gSet.LogFont.lfWidth && gSet.LogFont.lfHeight );
        if (!gSet.LogFont.lfWidth | !gSet.LogFont.lfHeight)
            return;
        TODO("X координаты нам известны, так что можно бы более корректно позицию определять...")
        
        INPUT_RECORD r; memset(&r, 0, sizeof(r));
        r.EventType = MOUSE_EVENT;
        TODO("а здесь хорошо бы получать известные координаты символов, а не простым делением")
        r.Event.MouseEvent.dwMousePosition = MakeCoord(x/gSet.LogFont.lfWidth, y/gSet.LogFont.lfHeight);
        
        // Mouse Buttons
        if (messg != WM_LBUTTONUP && (messg == WM_LBUTTONDOWN || messg == WM_LBUTTONDBLCLK || isPressed(VK_LBUTTON)))
            r.Event.MouseEvent.dwButtonState |= FROM_LEFT_1ST_BUTTON_PRESSED;
        if (messg != WM_RBUTTONUP && (messg == WM_RBUTTONDOWN || messg == WM_RBUTTONDBLCLK || isPressed(VK_RBUTTON)))
            r.Event.MouseEvent.dwButtonState |= RIGHTMOST_BUTTON_PRESSED;
        if (messg != WM_MBUTTONUP && (messg == WM_MBUTTONDOWN || messg == WM_MBUTTONDBLCLK || isPressed(VK_MBUTTON)))
            r.Event.MouseEvent.dwButtonState |= FROM_LEFT_2ND_BUTTON_PRESSED;
        if (messg != WM_XBUTTONUP && (messg == WM_XBUTTONDOWN || messg == WM_XBUTTONDBLCLK)) {
	        if ((HIWORD(wParam) & 0x0001/*XBUTTON1*/))
	            r.Event.MouseEvent.dwButtonState |= 0x0008/*FROM_LEFT_3ND_BUTTON_PRESSED*/;
	        else if ((HIWORD(wParam) & 0x0002/*XBUTTON2*/))
	            r.Event.MouseEvent.dwButtonState |= 0x0010/*FROM_LEFT_4ND_BUTTON_PRESSED*/;
        }

        // Key modifiers
        if (GetKeyState(VK_CAPITAL) & 1)
            r.Event.MouseEvent.dwControlKeyState |= CAPSLOCK_ON;
        if (GetKeyState(VK_NUMLOCK) & 1)
            r.Event.MouseEvent.dwControlKeyState |= NUMLOCK_ON;
        if (GetKeyState(VK_SCROLL) & 1)
            r.Event.MouseEvent.dwControlKeyState |= SCROLLLOCK_ON;
        if (isPressed(VK_LMENU))
            r.Event.MouseEvent.dwControlKeyState |= LEFT_ALT_PRESSED;
        if (isPressed(VK_RMENU))
            r.Event.MouseEvent.dwControlKeyState |= RIGHT_ALT_PRESSED;
        if (isPressed(VK_LCONTROL))
            r.Event.MouseEvent.dwControlKeyState |= LEFT_CTRL_PRESSED;
        if (isPressed(VK_RCONTROL))
            r.Event.MouseEvent.dwControlKeyState |= RIGHT_CTRL_PRESSED;
        if (isPressed(VK_SHIFT))
            r.Event.MouseEvent.dwControlKeyState |= SHIFT_PRESSED;

        if (messg == WM_LBUTTONDBLCLK || messg == WM_RBUTTONDBLCLK || messg == WM_MBUTTONDBLCLK)
            r.Event.MouseEvent.dwEventFlags = DOUBLE_CLICK;
        else if (messg == WM_MOUSEMOVE)
            r.Event.MouseEvent.dwEventFlags = MOUSE_MOVED;
        else if (messg == WM_MOUSEWHEEL) {
            r.Event.MouseEvent.dwEventFlags = MOUSE_WHEELED;
            r.Event.MouseEvent.dwButtonState |= (0xFFFF0000 & wParam);
        } else if (messg == WM_MOUSEHWHEEL) {
            r.Event.MouseEvent.dwEventFlags = 8; //MOUSE_HWHEELED
            r.Event.MouseEvent.dwButtonState |= (0xFFFF0000 & wParam);
        }

        // Посылаем событие в консоль через ConEmuC
        SendConsoleEvent ( &r );

        /*DWORD dwWritten = 0;
        if (!WriteConsoleInput(hConIn(), &r, 1, &dwWritten)) {
            DisplayLastError(L"SendMouseEvent failed!");
        }*/
    }
	//else {
	//       /*Если пересылать так - лезут глюки:
	//       1) левый клик вообще не доходит до консоли
	//       2) правый клик вызывает "левую" консольную менюшку
	//       3) проблемы с активацией окна(?)*/
	//       /* НО! тогда не работает выделение мышкой */
	//       static bool bInitialized = false;
	//       if (!bInitialized && (messg == WM_LBUTTONDOWN || messg == WM_RBUTTONDOWN))
	//       {
	//           bInitialized = true; // выполнить эту операцию один раз
	//           /*if (gConEmu.isConmanAlternative()) {
	//               POSTMESSAGE(hConWnd, WM_COMMAND, SC_PASTE_SECRET, 0, TRUE);
	//           } else {*/
	//               POSTMESSAGE(hConWnd, WM_COMMAND, (messg == WM_LBUTTONDOWN) ? SC_MARK_SECRET : SC_PASTE_SECRET, 0, TRUE);
	//               if (messg == WM_RBUTTONDOWN)
	//                   return; // достаточно SC_PASTE_SECRET
	//           //}
	//       }

	//       RECT conRect;
	//       GetClientRect(hConWnd, &conRect);
	//       short newX = MulDiv(x, conRect.right, klMax<uint>(1, mp_VCon->Width));
	//       short newY = MulDiv(y, conRect.bottom, klMax<uint>(1, mp_VCon->Height));

	//       POSTMESSAGE(hConWnd, messg, wParam, MAKELPARAM( newX, newY ), TRUE);
	//   }
}

void CRealConsole::SendConsoleEvent(INPUT_RECORD* piRec)
{
    DWORD dwErr = 0, dwMode = 0;
    BOOL fSuccess = FALSE;

    // Пайп есть. Проверим, что ConEmuC жив
    DWORD dwExitCode = 0;
    fSuccess = GetExitCodeProcess(mh_ConEmuC, &dwExitCode);
    if (dwExitCode!=STILL_ACTIVE) {
        //DisplayLastError(L"ConEmuC was terminated");
        return;
    }

	LogInput(piRec);

    TODO("Если пайп с таким именем не появится в течении 10 секунд (минуты?) - закрыть VirtualConsole показав ошибку");
    if (mh_ConEmuCInput==NULL || mh_ConEmuCInput==INVALID_HANDLE_VALUE) {
        // Try to open a named pipe; wait for it, if necessary. 
        DWORD dwSteps = 100;
        while (dwSteps--) 
        { 
          mh_ConEmuCInput = CreateFile( 
             ms_ConEmuCInput_Pipe,// pipe name 
             GENERIC_WRITE, 
             0,              // no sharing 
             NULL,           // default security attributes
             OPEN_EXISTING,  // opens existing pipe 
             0,              // default attributes 
             NULL);          // no template file 

          // Break if the pipe handle is valid. 
          if (mh_ConEmuCInput != INVALID_HANDLE_VALUE) 
             break; 

          // Exit if an error other than ERROR_PIPE_BUSY occurs. 
          dwErr = GetLastError();
          if (dwErr != ERROR_PIPE_BUSY) 
          {
            TODO("Подождать, пока появится пайп с таким именем, но только пока жив mh_ConEmuC");
            dwErr = WaitForSingleObject(mh_ConEmuC, 100);
            if (dwErr = WAIT_OBJECT_0)
                return;
            continue;
            //DisplayLastError(L"Could not open pipe", dwErr);
            //return 0;
          }

          // All pipe instances are busy, so wait for 1 second.
          if (!WaitNamedPipe(ms_ConEmuCInput_Pipe, 1000) ) 
          {
            dwErr = WaitForSingleObject(mh_ConEmuC, 100);
            if (dwErr = WAIT_OBJECT_0) {
                DEBUGSTR(L" - FAILED!\n");
                return;
            }
            //DisplayLastError(L"WaitNamedPipe failed"); 
            //return 0;
          }
        } 

        // The pipe connected; change to message-read mode. 
        dwMode = PIPE_READMODE_MESSAGE; 
        fSuccess = SetNamedPipeHandleState( 
          mh_ConEmuCInput,    // pipe handle 
          &dwMode,  // new pipe mode 
          NULL,     // don't set maximum bytes 
          NULL);    // don't set maximum time 
        if (!fSuccess) 
        {
          DEBUGSTR(L" - FAILED!\n");
		  SafeCloseHandle(mh_ConEmuCInput);
          DisplayLastError(L"SetNamedPipeHandleState failed");
          return;
        }
    }
    
    // Пайп есть. Проверим, что ConEmuC жив
    dwExitCode = 0;
    fSuccess = GetExitCodeProcess(mh_ConEmuC, &dwExitCode);
    if (dwExitCode!=STILL_ACTIVE) {
        //DisplayLastError(L"ConEmuC was terminated");
        return;
    }
    
    DWORD dwSize = sizeof(INPUT_RECORD), dwWritten;
    fSuccess = WriteFile ( mh_ConEmuCInput, piRec, dwSize, &dwWritten, NULL);
    if (!fSuccess) {
        DisplayLastError(L"Can't send console event");
        return;
    }
}


void CRealConsole::StopSignal()
{
    if (!this)
        return;

    if (mn_ProcessCount) {
        CSection SPRC(&csPRC, &ncsTPRC);
        m_Processes.clear();
        SPRC.Leave();
        mn_ProcessCount = 0;
    }

    SetEvent(mh_TermEvent);

	if (!mn_InRecreate)
		gConEmu.OnVConTerminated(mp_VCon);
}

void CRealConsole::StopThread(BOOL abRecreating)
{
#ifdef _DEBUG
	/*
        HeapValidate(mh_Heap, 0, NULL);
	*/
#endif

	_ASSERT(abRecreating==mb_ProcessRestarted);

	OutputDebugString(L"Entering StopThread\n");

    if (mh_MonitorThread) {
        // выставление флагов и завершение нити
        StopSignal(); //SetEvent(mh_TermEvent);
		if (WaitForSingleObject(mh_MonitorThread, 300) != WAIT_OBJECT_0) {
			OutputDebugString(L"### Main Thread wating timeout, terminating...\n");
            TerminateThread(mh_MonitorThread, 1);
		} else {
			OutputDebugString(L"Main Thread closed normally\n");
		}
    }
    
    if (!abRecreating) {
	    SafeCloseHandle(mh_TermEvent);
	    SafeCloseHandle(mh_ForceReadEvent);
	    SafeCloseHandle(mh_EndUpdateEvent);
	    SafeCloseHandle(mh_Sync2WindowEvent);
	    SafeCloseHandle(mh_ConChanged);
	}
    SafeCloseHandle(mh_CursorChanged);
    
    SafeCloseHandle(mh_MonitorThread);
    
    if (abRecreating) {
	    hConWnd = NULL;
	    mn_ConEmuC_PID = 0;
	    SafeCloseHandle(mh_ConEmuC);
	    SafeCloseHandle(mh_ConEmuCInput);
	    // Имя пайпа для управления ConEmuC
	    ms_ConEmuC_Pipe[0] = 0;
	    ms_ConEmuCInput_Pipe[0] = 0;
	}
#ifdef _DEBUG
	/*
        HeapValidate(mh_Heap, 0, NULL);
	*/
#endif

	OutputDebugString(L"Leaving StopThread\n");
}


void CRealConsole::Box(LPCTSTR szText)
{
#ifdef _DEBUG
    _ASSERT(FALSE);
#endif
    MessageBox(NULL, szText, _T("ConEmu"), MB_ICONSTOP);
}


BOOL CRealConsole::isBufferHeight()
{
    if (!this)
        return FALSE;

	return con.bBufferHeight;

    ////TODO: вроде не нужно
    ////CSection SCON(&csCON, &ncsTCON);

    //BOOL lbScrollMode = FALSE;
    ////CONSOLE_SCREEN_BUFFER_INFO inf; memset(&inf, 0, sizeof(inf));
    //if (con.m_sbi.dwSize.Y>(con.m_sbi.srWindow.Bottom-con.m_sbi.srWindow.Top+1))
    //    lbScrollMode = TRUE;

    //return lbScrollMode;
}

BOOL CRealConsole::isConSelectMode()
{
    if (!this) return false;
    return mb_ConsoleSelectMode;
}

BOOL CRealConsole::isDetached()
{
    if (this == NULL) return FALSE;
    return mb_Detached;
}

BOOL CRealConsole::isFar()
{
    if (!this) return false;
    return GetFarPID()!=0;
}

LPCTSTR CRealConsole::GetTitle()
{
    // На старте mn_ProcessCount==0, а кнопку в тулбаре показывать уже нужно
    if (!this /*|| !mn_ProcessCount*/)
        return NULL;
    return Title;
}

LRESULT CRealConsole::OnScroll(int nDirection)
{
    // SB_LINEDOWN / SB_LINEUP / SB_PAGEDOWN / SB_PAGEUP
    TODO("Переделать в команду пайпа");
    POSTMESSAGE(hConWnd, WM_VSCROLL, nDirection, NULL, FALSE);
    return 0;
}

LRESULT CRealConsole::OnKeyboard(HWND hWnd, UINT messg, WPARAM wParam, LPARAM lParam)
{
    LRESULT result = 0;

#ifdef _DEBUG
	if (wParam != VK_LCONTROL && wParam != VK_RCONTROL && wParam != VK_CONTROL &&
		wParam != VK_LSHIFT && wParam != VK_RSHIFT && wParam != VK_SHIFT &&
		wParam != VK_LMENU && wParam != VK_RMENU && wParam != VK_MENU &&
		wParam != VK_LWIN && wParam != VK_RWIN)
	{
		wParam = wParam;
	}
	if (wParam == VK_CONTROL || wParam == VK_LCONTROL || wParam == VK_RCONTROL || wParam == 'C') {
		if (messg == WM_KEYDOWN || messg == WM_KEYUP || messg == WM_CHAR) {
			wchar_t szDbg[128];
			if (messg == WM_KEYDOWN)
				wsprintf(szDbg, L"WM_KEYDOWN(%i,0x%08X)\n", wParam, lParam);
			else if (messg == WM_KEYUP)
				wsprintf(szDbg, L"WM_KEYUP(%i,0x%08X)\n", wParam, lParam);
			else
				wsprintf(szDbg, L"WM_CHAR(%i,0x%08X)\n", wParam, lParam);
			OutputDebugString(szDbg);
		}
	}
#endif

    if (mb_ConsoleSelectMode && messg == WM_KEYDOWN && ((wParam == VK_ESCAPE) || (wParam == VK_RETURN)))
        mb_ConsoleSelectMode = false; //TODO: может как-то по другому определять?

    // Основная обработка 
    {
        if (messg == WM_SYSKEYDOWN) 
            if (wParam == VK_INSERT && lParam & (1<<29)/*Бред. это 29-й бит, а не число 29*/)
                mb_ConsoleSelectMode = true;

        static bool isSkipNextAltUp = false;
        if (messg == WM_SYSKEYDOWN && wParam == VK_RETURN && lParam & (1<<29)/*Бред. это 29-й бит, а не число 29*/)
        {
            if (gSet.isSentAltEnter)
            {
                TODO("Переделать в SendConsoleInput");
                OnKeyboard(hConWnd, WM_KEYDOWN, VK_MENU, 0);
                OnKeyboard(hConWnd, WM_KEYDOWN, VK_RETURN, 0);
                OnKeyboard(hConWnd, WM_KEYUP, VK_RETURN, 0);
                OnKeyboard(hConWnd, WM_KEYUP, VK_MENU, 0);
            }
            else
            {
                if (isPressed(VK_SHIFT))
                    return 0;

                if (!gSet.isFullScreen)
                    gConEmu.SetWindowMode(rFullScreen);
                else
                    gConEmu.SetWindowMode(gConEmu.isWndNotFSMaximized ? rMaximized : rNormal);

                isSkipNextAltUp = true;
            }
        }
        else if (messg == WM_SYSKEYDOWN && wParam == VK_SPACE && lParam & (1<<29)/*Бред. это 29-й бит, а не число 29*/ && !isPressed(VK_SHIFT))
        {
            RECT rect, cRect;
            GetWindowRect(ghWnd, &rect);
            GetClientRect(ghWnd, &cRect);
            WINDOWINFO wInfo;   GetWindowInfo(ghWnd, &wInfo);
            gConEmu.ShowSysmenu(ghWnd, ghWnd, rect.right - cRect.right - wInfo.cxWindowBorders, rect.bottom - cRect.bottom - wInfo.cyWindowBorders);
        }
        else if (messg == WM_KEYUP && wParam == VK_MENU && isSkipNextAltUp) isSkipNextAltUp = false;
        else if (messg == WM_SYSKEYDOWN && wParam == VK_F9 && lParam & (1<<29)/*Бред. это 29-й бит, а не число 29*/ && !isPressed(VK_SHIFT))
            gConEmu.SetWindowMode((IsZoomed(ghWnd)||(gSet.isFullScreen&&gConEmu.isWndNotFSMaximized)) ? rNormal : rMaximized);
        else {
            INPUT_RECORD r = {KEY_EVENT};

            WORD nCaps = 1 & (WORD)GetKeyState(VK_CAPITAL);
            WORD nNum = 1 & (WORD)GetKeyState(VK_NUMLOCK);
            WORD nScroll = 1 & (WORD)GetKeyState(VK_SCROLL);
            WORD nLAlt = 0x8000 & (WORD)GetKeyState(VK_LMENU);
            WORD nRAlt = 0x8000 & (WORD)GetKeyState(VK_RMENU);
            WORD nLCtrl = 0x8000 & (WORD)GetKeyState(VK_LCONTROL);
            WORD nRCtrl = 0x8000 & (WORD)GetKeyState(VK_RCONTROL);
            WORD nShift = 0x8000 & (WORD)GetKeyState(VK_SHIFT);

            if (messg == WM_CHAR || messg == WM_SYSCHAR) {
                if (((WCHAR)wParam) <= 32 || mn_LastVKeyPressed == 0)
                    return 0; // это уже обработано
                r.Event.KeyEvent.bKeyDown = TRUE;
                r.Event.KeyEvent.uChar.UnicodeChar = (WCHAR)wParam;
                r.Event.KeyEvent.wRepeatCount = 1; TODO("0-15 ? Specifies the repeat count for the current message. The value is the number of times the keystroke is autorepeated as a result of the user holding down the key. If the keystroke is held long enough, multiple messages are sent. However, the repeat count is not cumulative.");
                r.Event.KeyEvent.wVirtualKeyCode = mn_LastVKeyPressed;
            } else {
                mn_LastVKeyPressed = wParam & 0xFFFF;
                //POSTMESSAGE(hConWnd, messg, wParam, lParam, FALSE);
                if ((wParam >= VK_F1 && wParam <= /*VK_F24*/ VK_SCROLL) || wParam <= 32 ||
                    (wParam >= VK_LSHIFT/*0xA0*/ && wParam <= /*VK_RMENU=0xA5*/ 0xB7 /*=VK_LAUNCH_APP2*/) ||
                    (wParam >= VK_LWIN/*0x5B*/ && wParam <= VK_APPS/*0x5D*/) ||
                    /*(wParam >= VK_NUMPAD0 && wParam <= VK_DIVIDE) ||*/ //TODO:
                    (wParam >= VK_PRIOR/*0x21*/ && wParam <= VK_HELP/*0x2F*/) ||
                    nLCtrl || nRCtrl ||
                    FALSE)
                {
                    r.Event.KeyEvent.wRepeatCount = 1; TODO("0-15 ? Specifies the repeat count for the current message. The value is the number of times the keystroke is autorepeated as a result of the user holding down the key. If the keystroke is held long enough, multiple messages are sent. However, the repeat count is not cumulative.");
                    r.Event.KeyEvent.wVirtualKeyCode = mn_LastVKeyPressed;
                    r.Event.KeyEvent.uChar.UnicodeChar = 0;
                    if (!nLCtrl && !nRCtrl) {
                        if (wParam == VK_ESCAPE || wParam == VK_RETURN || wParam == VK_BACK || wParam == VK_TAB || wParam == VK_SPACE
                            || FALSE)
                            r.Event.KeyEvent.uChar.UnicodeChar = wParam;
                    }
                    mn_LastVKeyPressed = 0; // чтобы не обрабатывать WM_(SYS)CHAR
                } else {
                    return 0;
                }
                r.Event.KeyEvent.bKeyDown = (messg == WM_KEYDOWN || messg == WM_SYSKEYDOWN);
            }

            r.Event.KeyEvent.wVirtualScanCode = ((DWORD)lParam & 0xFF0000) >> 16; // 16-23 - Specifies the scan code. The value depends on the OEM.
            // 24 - Specifies whether the key is an extended key, such as the right-hand ALT and CTRL keys that appear on an enhanced 101- or 102-key keyboard. The value is 1 if it is an extended key; otherwise, it is 0.
            // 29 - Specifies the context code. The value is 1 if the ALT key is held down while the key is pressed; otherwise, the value is 0.
            // 30 - Specifies the previous key state. The value is 1 if the key is down before the message is sent, or it is 0 if the key is up.
            // 31 - Specifies the transition state. The value is 1 if the key is being released, or it is 0 if the key is being pressed.
            r.Event.KeyEvent.dwControlKeyState = 0;
            if (((DWORD)lParam & (DWORD)(1 << 24)) != 0)
                r.Event.KeyEvent.dwControlKeyState |= ENHANCED_KEY;
            if ((nCaps & 1) == 1)
                r.Event.KeyEvent.dwControlKeyState |= CAPSLOCK_ON;
            if ((nNum & 1) == 1)
                r.Event.KeyEvent.dwControlKeyState |= NUMLOCK_ON;
            if ((nScroll & 1) == 1)
                r.Event.KeyEvent.dwControlKeyState |= SCROLLLOCK_ON;
            if (nLAlt & 0x8000)
                r.Event.KeyEvent.dwControlKeyState |= LEFT_ALT_PRESSED;
            if (nRAlt & 0x8000)
                r.Event.KeyEvent.dwControlKeyState |= RIGHT_ALT_PRESSED;
            if (nLCtrl & 0x8000)
                r.Event.KeyEvent.dwControlKeyState |= LEFT_CTRL_PRESSED;
            if (nRCtrl & 0x8000)
                r.Event.KeyEvent.dwControlKeyState |= RIGHT_CTRL_PRESSED;
            if (nShift & 0x8000)
                r.Event.KeyEvent.dwControlKeyState |= SHIFT_PRESSED;
            SendConsoleEvent(&r);

            if (messg == WM_CHAR || messg == WM_SYSCHAR) {
                // И сразу посылаем отпускание
                r.Event.KeyEvent.bKeyDown = FALSE;
                SendConsoleEvent(&r);
            }
        }
    }

    /*if (IsDebuggerPresent()) {
        if (hWnd ==ghWnd)
            DEBUGSTR(L"   focused ghWnd\n"); else
        if (hWnd ==hConWnd)
            DEBUGSTR(L"   focused hConWnd\n"); else
        if (hWnd ==ghWndDC)
            DEBUGSTR(L"   focused ghWndDC\n"); 
        else
            DEBUGSTR(L"   focused UNKNOWN\n"); 
    }*/

    return 0;
}

void CRealConsole::OnWinEvent(DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
    _ASSERTE(hwnd!=NULL);
    
    if (hConWnd == NULL && event == EVENT_CONSOLE_START_APPLICATION && idObject == mn_ConEmuC_PID)
        SetHwnd ( hwnd );

    _ASSERTE(hConWnd!=NULL && hwnd==hConWnd);
    
    //TODO("!!! Сделать обработку событий и население m_Processes");
    //
    //AddProcess(idobject), и удаление idObject из списка процессов
    // Не забыть, про обработку флажка Ntvdm
    
    TODO("При отцеплении от консоли NTVDM - можно прибить этот процесс");
    switch(event)
    {
    case EVENT_CONSOLE_START_APPLICATION:
        //A new console process has started. 
        //The idObject parameter contains the process identifier of the newly created process. 
        //If the application is a 16-bit application, the idChild parameter is CONSOLE_APPLICATION_16BIT and idObject is the process identifier of the NTVDM session associated with the console.
        {   
	        if (mn_InRecreate>=1)
		        mn_InRecreate = 0; // корневой процесс успешно пересоздался
            ProcessAdd(idObject);
            // Если запущено 16битное приложение - необходимо повысить приоритет нашего процесса, иначе будут тормоза
            if (idChild == CONSOLE_APPLICATION_16BIT) {
                mn_ActiveStatus |= CES_NTVDM;
                SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
            }
        } break;
    case EVENT_CONSOLE_END_APPLICATION:
        //A console process has exited. 
        //The idObject parameter contains the process identifier of the terminated process.
        {
            ProcessDelete(idObject);
            //
            if (idChild == CONSOLE_APPLICATION_16BIT) {
                gConEmu.gbPostUpdateWindowSize = true;
                gConEmu.mn_ActiveStatus &= ~CES_NTVDM;
                TODO("Вообще-то хорошо бы проверить, что 16бит не осталось в других консолях");
                SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
            }
        } break;
    case EVENT_CONSOLE_UPDATE_REGION: // 0x4002 
        {
        //More than one character has changed. 
        //The idObject parameter is a COORD structure that specifies the start of the changed region. 
        //The idChild parameter is a COORD structure that specifies the end of the changed region.
            SetEvent(mh_ConChanged);
        } break;
    case EVENT_CONSOLE_UPDATE_SCROLL: //0x4004
        {
        //The console has scrolled.
        //The idObject parameter is the horizontal distance the console has scrolled. 
        //The idChild parameter is the vertical distance the console has scrolled.
            SetEvent(mh_ConChanged);
        } break;
    case EVENT_CONSOLE_UPDATE_SIMPLE: //0x4003
        {
        //A single character has changed.
        //The idObject parameter is a COORD structure that specifies the character that has changed.
        //Warning! В писании от  микрософта тут ошибка!
        //The idChild parameter specifies the character in the low word and the character attributes in the high word.
            COORD crWhere; memmove(&crWhere, &idObject, sizeof(idObject));
            WCHAR ch = (WCHAR)LOWORD(idChild); WORD wA = HIWORD(idChild);
            CSection cs(&con.cs, &con.ncsT);
            HANDLE hEvent = NULL;
            try {
                if (con.pConChar && con.pConAttr && !isBufferHeight()) {
                    int nIdx = crWhere.X+crWhere.Y*con.m_sbi.dwSize.X;
                    con.pConChar[nIdx] = ch;
                    con.pConAttr[nIdx] = wA;
                    hEvent = mh_ForceReadEvent;
                } else {
                    hEvent = mh_ConChanged; TODO("Вообще-то можно и без запроса в консоль - самим сразу менять, но нужно учесть, что может быть прокрутка");
                }
            } catch(...) {
                _ASSERT(FALSE);
            }
            cs.Leave();
            if (hEvent) SetEvent(hEvent);
        } break;
    case EVENT_CONSOLE_CARET: //0x4001
        {
        //Warning! WinXPSP3. Это событие проходит ТОЛЬКО если консоль в фокусе. 
        //         А с ConEmu она НИКОГДА не в фокусе, так что курсор не обновляется.
        //The console caret has moved.
        //The idObject parameter is one or more of the following values:
        //      CONSOLE_CARET_SELECTION or CONSOLE_CARET_VISIBLE.
        //The idChild parameter is a COORD structure that specifies the cursor's current position.
            COORD crWhere; memmove(&crWhere, &idChild, sizeof(idChild));
            TODO("А тут тоже можно самим курсор позиционировать, не забыть про прокрутку");
            if (isBufferHeight()) {
                SetEvent(mh_CursorChanged); 
            } else {
                con.m_sbi.dwCursorPosition = crWhere;
                SetEvent(mh_ForceReadEvent);
            }
        } break;
    case EVENT_CONSOLE_LAYOUT: //0x4005
        {
        //The console layout has changed.
            SetEvent(mh_ConChanged);
        } break;
    }
}


DWORD CRealConsole::ServerThread(LPVOID lpvParam) 
{ 
    CRealConsole *pRCon = (CRealConsole*)lpvParam;
    BOOL fConnected = FALSE;
    DWORD dwErr = 0;
    HANDLE hPipe = NULL; 
    HANDLE hWait[2] = {NULL,NULL};
	DWORD dwTID = GetCurrentThreadId();

    _ASSERTE(pRCon->hConWnd!=NULL);
    _ASSERTE(pRCon->ms_VConServer_Pipe[0]!=0);
    _ASSERTE(pRCon->mh_ServerSemaphore!=NULL);
    //wsprintf(pRCon->ms_VConServer_Pipe, CEGUIPIPENAME, L".", (DWORD)pRCon->hConWnd); //был mn_ConEmuC_PID

    // The main loop creates an instance of the named pipe and 
    // then waits for a client to connect to it. When the client 
    // connects, a thread is created to handle communications 
    // with that client, and the loop is repeated. 
    
    hWait[0] = pRCon->mh_TermEvent;
    hWait[1] = pRCon->mh_ServerSemaphore;

    // Пока не затребовано завершение консоли
    do {
        while (!fConnected)
        { 
            _ASSERTE(hPipe == NULL);

            // Дождаться разрешения семафора, или закрытия консоли
            dwErr = WaitForMultipleObjects ( 2, hWait, FALSE, INFINITE );
            if (dwErr == WAIT_OBJECT_0) {
                return 0; // Консоль закрывается
            }

			for (int i=0; i<MAX_SERVER_THREADS; i++) {
				if (pRCon->mn_ServerThreadsId[i] == dwTID) {
					pRCon->mh_ActiveServerThread = pRCon->mh_ServerThreads[i]; break;
				}
			}

            hPipe = CreateNamedPipe( 
                pRCon->ms_VConServer_Pipe, // pipe name 
                PIPE_ACCESS_DUPLEX,       // read/write access 
                PIPE_TYPE_MESSAGE |       // message type pipe 
                PIPE_READMODE_MESSAGE |   // message-read mode 
                PIPE_WAIT,                // blocking mode 
                PIPE_UNLIMITED_INSTANCES, // max. instances  
                PIPEBUFSIZE,              // output buffer size 
                PIPEBUFSIZE,              // input buffer size 
                0,                        // client time-out 
                NULL);                    // default security attribute 

            _ASSERTE(hPipe != INVALID_HANDLE_VALUE);

            if (hPipe == INVALID_HANDLE_VALUE) 
            {
                //DisplayLastError(L"CreatePipe failed"); 
                hPipe = NULL;
                Sleep(50);
                continue;
            }

            // Wait for the client to connect; if it succeeds, 
            // the function returns a nonzero value. If the function
            // returns zero, GetLastError returns ERROR_PIPE_CONNECTED. 

            fConnected = ConnectNamedPipe(hPipe, NULL) ? TRUE : ((dwErr = GetLastError()) == ERROR_PIPE_CONNECTED); 

            // Консоль закрывается!
            if (WaitForSingleObject ( pRCon->mh_TermEvent, 0 ) == WAIT_OBJECT_0) {
                //FlushFileBuffers(hPipe); -- это не нужно, мы ничего не возвращали
                //DisconnectNamedPipe(hPipe); 
				ReleaseSemaphore(pRCon->mh_ServerSemaphore, 1, NULL);
                SafeCloseHandle(hPipe);
                return 0;
            }

            if (!fConnected)
                SafeCloseHandle(hPipe);
        }

        if (fConnected) {
            // сразу сбросим, чтобы не забыть
            fConnected = FALSE;
            // разрешить другой нити принять вызов
            ReleaseSemaphore(pRCon->mh_ServerSemaphore, 1, NULL);
            
            /*{ // Запустить новый серверный пайп. Этот instance будет закрыт после обработки команды.
                DWORD dwServerTID = 0;
                HANDLE hThread = CreateThread(NULL, 0, ServerThread, (LPVOID)pRCon, 0, &dwServerTID);
                _ASSERTE(hThread!=NULL);
                SafeCloseHandle(hThread);
            }*/

            //ServerThreadCommandArg* pArg = (ServerThreadCommandArg*)calloc(sizeof(ServerThreadCommandArg),1);
            //pArg->pRCon = pRCon;
            //pArg->hPipe = hPipe;
            pRCon->ServerThreadCommand ( hPipe ); // При необходимости - записывает в пайп результат сама
            //DWORD dwCommandTID = 0;
            //HANDLE hCommandThread = CreateThread(NULL, 0, ServerThreadCommand, (LPVOID)pArg, 0, &dwCommandTID);
            //_ASSERTE(hCommandThread!=NULL);
            //SafeCloseHandle(hCommandThread);
        }

        FlushFileBuffers(hPipe); 
        //DisconnectNamedPipe(hPipe); 
        SafeCloseHandle(hPipe);
    } // Перейти к открытию нового instance пайпа
    while (WaitForSingleObject ( pRCon->mh_TermEvent, 0 ) != WAIT_OBJECT_0);

    return 0; 
}

void CRealConsole::ServerThreadCommand(HANDLE hPipe)
{
    CESERVER_REQ in={0}, *pIn=NULL;
    DWORD cbRead = 0, cbWritten = 0, dwErr = 0;
    BOOL fSuccess = FALSE;

    // Send a message to the pipe server and read the response. 
    fSuccess = ReadFile( 
        hPipe,            // pipe handle 
        &in,              // buffer to receive reply
        sizeof(in),       // size of read buffer
        &cbRead,          // bytes read
        NULL);            // not overlapped 

    if (!fSuccess && ((dwErr = GetLastError()) != ERROR_MORE_DATA)) 
    {
        _ASSERTE("ReadFile(pipe) failed"==NULL);
        //CloseHandle(hPipe);
        return;
    }
    _ASSERTE(in.nSize>=12 && cbRead>=12);
    _ASSERTE(in.nVersion == CESERVER_REQ_VER);
    if (cbRead < 12 || /*in.nSize < cbRead ||*/ in.nVersion != CESERVER_REQ_VER) {
        //CloseHandle(hPipe);
        return;
    }

    int nAllSize = in.nSize;
    pIn = (CESERVER_REQ*)calloc(nAllSize,1);
    _ASSERTE(pIn!=NULL);
    memmove(pIn, &in, cbRead);
    _ASSERTE(pIn->nVersion==CESERVER_REQ_VER);

    LPBYTE ptrData = ((LPBYTE)pIn)+cbRead;
    nAllSize -= cbRead;

    while(nAllSize>0)
    { 
        //_tprintf(TEXT("%s\n"), chReadBuf);

        // Break if TransactNamedPipe or ReadFile is successful
        if(fSuccess)
            break;

        // Read from the pipe if there is more data in the message.
        fSuccess = ReadFile( 
            hPipe,      // pipe handle 
            ptrData,    // buffer to receive reply 
            nAllSize,   // size of buffer 
            &cbRead,    // number of bytes read 
            NULL);      // not overlapped 

        // Exit if an error other than ERROR_MORE_DATA occurs.
        if( !fSuccess && ((dwErr = GetLastError()) != ERROR_MORE_DATA)) 
            break;
        ptrData += cbRead;
        nAllSize -= cbRead;
    }

    TODO("Может возникнуть ASSERT, если консоль была закрыта в процессе чтения");
    _ASSERTE(nAllSize==0);
    if (nAllSize>0) {
        //CloseHandle(hPipe);
        return; // удалось считать не все данные
    }
    
    UINT nDataSize = pIn->nSize - sizeof(CESERVER_REQ) + 1;

    // Все данные из пайпа получены, обрабатываем команду и возвращаем (если нужно) результат
    if (pIn->nCmd == CECMD_GETFULLINFO || pIn->nCmd == CECMD_GETSHORTINFO) {
        ApplyConsoleInfo(pIn);

	} else if (pIn->nCmd == CECMD_CMDSTARTSTOP) {
		//
		BOOL  bStarted = *(DWORD*)(pIn->Data);
		HWND  hWnd     = (HWND)*(DWORD*)(pIn->Data+4);
		DWORD nPID     = *(DWORD*)(pIn->Data+8);
		
		if (bStarted) {
			int newWidth = TextWidth(), newHeight = TextHeight();
			if (gSet.DefaultBufferHeight && !isBufferHeight()) {
				// скорректировать ширину окна на ширину появляющейся полосы прокрутки
				RECT rcWnd; GetClientRect(ghWnd, &rcWnd);
				rcWnd.right -= GetSystemMetrics(SM_CXVSCROLL);
				if (rcWnd.right > rcWnd.left) { // так должно быть
					RECT rcNewCon = gConEmu.CalcRect(CER_CONSOLE, rcWnd, CER_MAINCLIENT);
					newWidth = rcNewCon.right;
				}
			}
			((DWORD*)(pIn->Data))[0] = gSet.DefaultBufferHeight;
			((DWORD*)(pIn->Data))[1] = newWidth;
			((DWORD*)(pIn->Data))[2] = newHeight;

			ProcessAdd(nPID);
		} else {
			ProcessDelete(nPID);
		}
        
		// Отправляем
		fSuccess = WriteFile( 
			hPipe,        // handle to pipe 
			pIn,         // buffer to write from 
			pIn->nSize,  // number of bytes to write 
			&cbWritten,   // number of bytes written 
			NULL);        // not overlapped I/O 

    } else if (pIn->nCmd == CECMD_GETGUIHWND) {
        CESERVER_REQ *pRet = NULL;
        int nSize = sizeof(CESERVER_REQ) - sizeof(pRet->Data/*BYTE*/) + 2*sizeof(DWORD);
        pRet = (CESERVER_REQ*)calloc(nSize, 1);
        pRet->nSize = nSize;
        pRet->nCmd = pIn->nCmd;
        pRet->nVersion = CESERVER_REQ_VER;
        ((DWORD*)pRet->Data)[0] = (DWORD)ghWnd;
        ((DWORD*)pRet->Data)[1] = (DWORD)ghWndDC;
        // Отправляем
        fSuccess = WriteFile( 
            hPipe,        // handle to pipe 
            pRet,         // buffer to write from 
            pRet->nSize,  // number of bytes to write 
            &cbWritten,   // number of bytes written 
            NULL);        // not overlapped I/O 
            
    } else if (pIn->nCmd == CECMD_TABSCHANGED) {
		_ASSERTE(nDataSize>=4);
	    int nTabCount = (nDataSize-4) / sizeof(ConEmuTab);
	    _ASSERTE((nTabCount*sizeof(ConEmuTab))==(nDataSize-4));
	    SetTabs((ConEmuTab*)(pIn->Data+4), nTabCount);
    }

    // Освободить память
    free(pIn);

    //CloseHandle(hPipe);
    return;
}

#define COPYBUFFERS(v,s) { \
    nVarSize = s; \
    if ((lpCur+nVarSize)>lpEnd) { \
        _ASSERT(FALSE); \
        return; \
    } \
    memmove(&(v), lpCur, nVarSize); \
    lpCur += nVarSize; \
}
#define COPYBUFFER(v) { \
    COPYBUFFERS(v, sizeof(v)); \
}

WARNING("Для ограничения доступа к содержимому возможно лучше использовать Semaphore nor CriticalSection");
void CRealConsole::ApplyConsoleInfo(CESERVER_REQ* pInfo)
{
    _ASSERTE(this!=NULL);
    if (this==NULL) return;

	LogPacket(pInfo);

    int nPacket = con.nPacketIdx++;
    if (gSet.isAdvLogging && gSet.szDumpPackets[0]) {
        wchar_t szPath[MAX_PATH+20]; wsprintf(szPath, gSet.szDumpPackets, mn_ConEmuC_PID, nPacket);
        HANDLE hFile = CreateFile(szPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 
            FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile!=INVALID_HANDLE_VALUE) {
            DWORD dwWritten = 0;
            WriteFile(hFile, pInfo, pInfo->nSize, &dwWritten, 0);
            CloseHandle(hFile);
        }
    }
    BOOL bBufRecreated = FALSE;
    // Теперь нужно раскидать данные про структурам
    LPBYTE lpCur = (LPBYTE)pInfo->Data;
    LPBYTE lpEnd = ((LPBYTE)pInfo)+pInfo->nSize;
    DWORD  nVarSize;
    // 1
    HWND hWnd = NULL;
    COPYBUFFER(hWnd);
    _ASSERTE(hWnd!=NULL);
    if (hConWnd != hWnd) {
        SetHwnd ( hWnd );
    }
    // 2 - GetTickCount последнего чтения
    DWORD nLastConReadTick = 0;
    COPYBUFFER(nLastConReadTick);
    // 3
    // во время чтения содержимого консоли может увеличиться количество процессов
    // поэтому РЕАЛЬНОЕ количество - выставим после чтения и CriticalSection(csProc);
    //CSection SPRC(&csPRC, &ncsTPRC);
    //TODO("Сделать корректную обработку! не чистить m_Processes не глядя, а удалять только отсутствующие! также получить информацию о процессе");
    DWORD dwProcCount = 0;
    COPYBUFFER(dwProcCount);
    _ASSERTE(dwProcCount==0); // Список процессов формируется в GUI. Reserved...
    if (dwProcCount) lpCur += sizeof(DWORD)*dwProcCount;
    //m_Processes.clear();
    //ConProcess prc = {0};
    //while (dwProcCount>0) {
    //  prc.ProcessID = *((DWORD*)lpCur); lpCur += sizeof(DWORD);
    //  m_Processes.push_back(prc);
    //  dwProcCount--;
    //}
    //SPRC.Leave();

    // Теперь нужно открыть секцию - начинаем изменение переменных класса
    CSection sc(NULL,NULL);

    // 4
    DWORD dwSelRc = 0; //CONSOLE_SELECTION_INFO sel = {0}; // GetConsoleSelectionInfo
    COPYBUFFER(dwSelRc);
    if (dwSelRc != 0) {
        _ASSERTE(dwSelRc == sizeof(con.m_sel));
        COPYBUFFER(con.m_sel);
    }
    // 5
    DWORD dwCiRc = 0; //CONSOLE_CURSOR_INFO ci = {0}; // GetConsoleCursorInfo
    COPYBUFFER(dwCiRc);
    if (dwCiRc != 0) {
        _ASSERTE(dwCiRc == sizeof(con.m_ci));
        COPYBUFFER(con.m_ci);
    }
    // 6, 7, 8
    COPYBUFFER(con.m_dwConsoleCP);       // GetConsoleCP()
    COPYBUFFER(con.m_dwConsoleOutputCP); // GetConsoleOutputCP()
    COPYBUFFER(con.m_dwConsoleMode);     // GetConsoleMode(hConIn, &dwConsoleMode);
    // 9
    DWORD dwSbiRc = 0; //CONSOLE_SCREEN_BUFFER_INFO sbi = {{0,0}}; // GetConsoleScreenBufferInfo
    COPYBUFFER(dwSbiRc);
    int nNewWidth = 0, nNewHeight = 0;
    if (dwSbiRc != 0) {
        _ASSERTE(dwSbiRc == sizeof(con.m_sbi));
        COPYBUFFER(con.m_sbi);
        if (GetConWindowSize(con.m_sbi, nNewWidth, nNewHeight)) {
			con.bBufferHeight = (nNewHeight < con.m_sbi.dwSize.Y);
				TODO("Включить прокрутку? или оно само?");
            if (nNewWidth != con.nTextWidth || nNewHeight != con.nTextHeight) {
                bBufRecreated = TRUE;
                sc.Enter(&con.cs, &con.ncsT);
                WARNING("может не заблокировалось?");
                InitBuffers(nNewWidth*nNewHeight*2);
            }
        }
		if (gSet.AutoScroll)
			con.nTopVisibleLine = con.m_sbi.srWindow.Top;
    }
    // 10
    DWORD dwCharChanged = 0;
    COPYBUFFER(dwCharChanged);
    if (dwCharChanged != 0) {
        _ASSERTE(dwCharChanged >= sizeof(CESERVER_CHAR));
        _ASSERTE(!bBufRecreated && con.pConChar && con.pConAttr);

        // Если буфер был пересоздан (bBufRecreated) изменение только блока символов бессмысленно - нужны полные данные!
        if (!bBufRecreated && con.pConChar && con.pConAttr) {
            CESERVER_CHAR* pch = (CESERVER_CHAR*)calloc(dwCharChanged,1);
            COPYBUFFERS(*pch,2*sizeof(COORD));
            int nLineLen = pch->crEnd.X - pch->crStart.X + 1;
            int nLineCount = pch->crEnd.Y - pch->crStart.Y + 1;
            _ASSERTE((nLineLen*nLineCount*4+2*sizeof(COORD))==dwCharChanged);
            COPYBUFFERS(pch->data,dwCharChanged-2*sizeof(COORD));
            //_ASSERTE(!isBufferHeight());
            wchar_t* pszLine = (wchar_t*)(pch->data);
            WORD*    pnLine  = ((WORD*)pch->data)+(nLineLen*nLineCount);
            // Собственно данные копировать в новые переменные - текущий буфер консоли
            for (int y = pch->crStart.Y; y <= pch->crEnd.Y; y++) {
				int nLine = y - con.nTopVisibleLine;
				if (nLine < 0 || nLine >= con.nTextHeight)
					continue; // Эта строка не попадает в видимую область
                int nIdx = pch->crStart.X + nLine * con.m_sbi.dwSize.X;
                memmove(con.pConChar+nIdx, pszLine, nLineLen*2);
                    pszLine += nLineLen;
                memmove(con.pConAttr+nIdx, pnLine, nLineLen*2);
                    pnLine += nLineLen;
            }
        } else {
            lpCur += dwCharChanged;
        }
    }
    // 11
    DWORD OneBufferSize = 0;
    COPYBUFFER(OneBufferSize);
    if (OneBufferSize != 0) {
	    BOOL lbObsolete = FALSE;
		int nDelta = (int)(DWORD)(nLastConReadTick - mn_LastConFullReadTick);
		if (nDelta<0) lbObsolete = TRUE;
	    
		if (!lbObsolete)
		{
		    sc.Enter(&con.cs, &con.ncsT);
	        if (InitBuffers(OneBufferSize)) {
	            memmove(con.pConChar, lpCur, OneBufferSize); lpCur += OneBufferSize;
	            memmove(con.pConAttr, lpCur, OneBufferSize); lpCur += OneBufferSize;
	        }
	    }
    }

    sc.Leave();

    //if (mn_LastConReadTick) {
    //    DWORD dwDelta = nLastConReadTick - mn_LastConReadTick;
    //    // может перевалить через 0
    //    _ASSERTE(mn_LastConReadTick <= nLastConReadTick || dwDelta > 0x1000000);
    //}
    //mn_LastConReadTick = nLastConReadTick;

    
    // Передернуть GUI
    SetEvent(mh_ForceReadEvent);
}

int CRealConsole::GetProcesses(ConProcess** ppPrc)
{
	if (mn_InRecreate) {
		DWORD dwCurTick = GetTickCount();
		DWORD dwDelta = dwCurTick - mn_InRecreate;
		if (dwDelta > CON_RECREATE_TIMEOUT) {
			mn_InRecreate = 0;
		} else if (ppPrc == NULL) {
			if (mn_InRecreate && !mb_ProcessRestarted && mh_ConEmuC) {
				DWORD dwExitCode = 0;
				if (GetExitCodeProcess(mh_ConEmuC, &dwExitCode) && dwExitCode != STILL_ACTIVE) {
					RecreateProcessStart();
				}
			}
			return 1; // Чтобы во время Recreate GUI не захлопнулся
		}
	}


    // Если хотят узнать только количество процессов
    if (ppPrc == NULL || mn_ProcessCount == 0) {
        if (ppPrc) *ppPrc = NULL;
        return mn_ProcessCount;
    }

    CSection SPRC(&csPRC, &ncsTPRC);
    
    int dwProcCount = m_Processes.size();

    if (dwProcCount > 0) {
        *ppPrc = (ConProcess*)calloc(dwProcCount, sizeof(ConProcess));
        _ASSERTE((*ppPrc)!=NULL);
        
        for (int i=0; i<dwProcCount; i++) {
            (*ppPrc)[i] = m_Processes[i];
        }
        
    } else {
        *ppPrc = NULL;
    }
    
    SPRC.Leave();
    return dwProcCount;
}

DWORD CRealConsole::GetActiveStatus()
{
    return mn_ActiveStatus;
}

DWORD CRealConsole::GetServerPID()
{
    return mn_ConEmuC_PID;
}

DWORD CRealConsole::GetFarPID()
{
    if (!this)
        return 0;

    if ((mn_ActiveStatus & CES_FARACTIVE) == 0)
        return 0;

    return mn_FarPID;
}

// Обновить статус запущенных программ
void CRealConsole::ProcessUpdateFlags(BOOL abProcessChanged)
{
    //Warning: Должен вызываться ТОЛЬКО из ProcessAdd/ProcessDelete, т.к. сам секцию не блокирует

    bool bIsFar=false, bIsTelnet=false, bIsNtvdm=false, bIsCmd=false;
    DWORD dwPID = 0;

    std::vector<ConProcess>::reverse_iterator iter = m_Processes.rbegin();
    while (iter!=m_Processes.rend()) {
        // Корневой процесс ConEmuC не учитываем!
        if (iter->ProcessID != mn_ConEmuC_PID) {
            if (!bIsFar && iter->IsFar) bIsFar = true;
            if (!bIsTelnet && iter->IsTelnet) bIsTelnet = true;
            if (!bIsNtvdm && iter->IsNtvdm) bIsNtvdm = true;
            if (!bIsCmd && iter->IsCmd) bIsCmd = true;
            // 
            if (!dwPID && iter->IsFar)  dwPID = iter->ProcessID;
        }
        iter++;
    }

    TODO("Однако, наверное cmd.exe может быть запущен и в 'фоне'? Например из Update");
    if (bIsCmd && bIsFar) { // Если в консоли запущен cmd.exe - значит (скорее всего?) фар выполняет команду
        bIsFar = false; dwPID = 0;
    }
        
    mn_ActiveStatus &= ~CES_PROGRAMS2;
    if (bIsFar) mn_ActiveStatus |= CES_FARACTIVE;
    if (bIsTelnet) mn_ActiveStatus |= CES_TELNETACTIVE;
    if (bIsNtvdm) mn_ActiveStatus |= CES_NTVDM;

    mn_ProcessCount = m_Processes.size();
    mn_FarPID = dwPID;

    if (mn_ProcessCount == 0) {
	    if (mn_InRecreate == 0) {
	        StopSignal();
	    } else if (mn_InRecreate == 1) {
		    mn_InRecreate = 2;
	    }
    }

    // Обновить список процессов в окне настроек и закладки
    if (abProcessChanged) {
        gConEmu.UpdateProcessDisplay(abProcessChanged);
        TabBar.Refresh(mn_ActiveStatus & CES_FARACTIVE);
    }
}

void CRealConsole::ProcessAdd(DWORD addPID)
{
    CSection SPRC(&csPRC, &ncsTPRC);

	BOOL lbRecreateOk = FALSE;
	if (mn_InRecreate && mn_ProcessCount == 0) {
		lbRecreateOk = TRUE;
	}
    
    std::vector<ConProcess>::iterator iter;
    BOOL bAlive = FALSE;
    BOOL bProcessChanged = FALSE;
    
    // может он уже есть в списке?
    iter=m_Processes.begin();
    while (iter!=m_Processes.end()) {
        if (addPID == iter->ProcessID) {
            addPID = 0;
            break;
        }
        iter++;
    }

    

    // Теперь нужно добавить новый процесс
    ConProcess cp;
    HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    _ASSERTE(h!=INVALID_HANDLE_VALUE);
    if (h==INVALID_HANDLE_VALUE) {
        DWORD dwErr = GetLastError();
        // Просто добавить, раз не удалось снять Snapshoot
        memset(&cp, 0, sizeof(cp));
        cp.ProcessID = addPID; cp.RetryName = true;
        wsprintf(cp.Name, L"Can't create snaphoot. ErrCode=0x%08X", dwErr);
        m_Processes.push_back(cp);
        
    } else {
        //Snapshoot создан, поехали

        // Перед добавлением нового - поставить пометочку на все процессы, вдруг кто уже убился 
        iter = m_Processes.begin();
        while (iter != m_Processes.end()) { iter->Alive = false; iter ++; }

        PROCESSENTRY32 p; memset(&p, 0, sizeof(p)); p.dwSize = sizeof(p);
        if (Process32First(h, &p)) 
        {
            do {
                // Если он addPID - добавить в m_Processes
                if (addPID && addPID == p.th32ProcessID) {
                    if (!bProcessChanged) bProcessChanged = TRUE;
                    memset(&cp, 0, sizeof(cp));
                    cp.ProcessID = addPID; cp.ParentPID = p.th32ParentProcessID;
                    ProcessCheckName(cp, p.szExeFile); //far, telnet, cmd, conemuc, и пр.
                    cp.Alive = true;
                    m_Processes.push_back(cp);
                    break;
                }
                
                // Перебираем запомненные процессы - поставить флажок Alive
                // сохранить имя для тех процессов, которым ранее это сделать не удалось
                iter = m_Processes.begin();
                while (iter != m_Processes.end())
                {
                    if (iter->ProcessID == p.th32ProcessID) {
                        iter->Alive = true;
                        if (!iter->NameChecked)
                        {
                            // пометить, что сменился список (определилось имя процесса)
                            if (!bProcessChanged) bProcessChanged = TRUE;
                            //far, telnet, cmd, conemuc, и пр.
                            ProcessCheckName(*iter, p.szExeFile);
                            // запомнить родителя
                            iter->ParentPID = p.th32ParentProcessID;
                        }
                    }
                    iter ++;
                }
                
            // Следущий процесс
            } while (Process32Next(h, &p));
        }
        
        // Убрать процессы, которых уже нет
        iter = m_Processes.begin();
        while (iter != m_Processes.end())
        {
            if (!iter->Alive) {
                if (!bProcessChanged) bProcessChanged = TRUE;
                iter = m_Processes.erase(iter);
            } else {
                iter ++;
            }
        }
        
        // Закрыть shapshoot
        SafeCloseHandle(h);
    }
    
    // Обновить статус запущенных программ, получить PID FAR'а, посчитать количество процессов в консоли
    ProcessUpdateFlags(bProcessChanged);

	// 
	if (lbRecreateOk)
		mn_InRecreate = 0;
}

void CRealConsole::ProcessDelete(DWORD addPID)
{
    CSection SPRC(&csPRC, &ncsTPRC);

    BOOL bProcessChanged = FALSE;
    std::vector<ConProcess>::iterator iter=m_Processes.begin();
    while (iter!=m_Processes.end()) {
        if (addPID == iter->ProcessID) {
            m_Processes.erase(iter);
            bProcessChanged = TRUE;
            break;
        }
        iter++;
    }

    // Обновить статус запущенных программ, получить PID FAR'а, посчитать количество процессов в консоли
    ProcessUpdateFlags(bProcessChanged);
    
    if (mn_InRecreate && mn_ProcessCount == 0 && !mb_ProcessRestarted) {
		RecreateProcessStart();
    }
}

void CRealConsole::ProcessCheckName(struct ConProcess &ConPrc, LPWSTR asFullFileName)
{
    wchar_t* pszSlash = _tcsrchr(asFullFileName, _T('\\'));
    if (pszSlash) pszSlash++; else pszSlash=asFullFileName;
    int nLen = _tcslen(pszSlash);
    if (nLen>=63) pszSlash[63]=0;
    lstrcpyW(ConPrc.Name, pszSlash);

    ConPrc.IsFar = lstrcmpi(ConPrc.Name, _T("far.exe"))==0;

    ConPrc.IsNtvdm = lstrcmpi(ConPrc.Name, _T("ntvdm.exe"))==0;

    ConPrc.IsTelnet = lstrcmpi(ConPrc.Name, _T("telnet.exe"))==0;
    
    TODO("Тут главное не промахнуться, и не посчитать корневой conemuc, из которого запущен сам FAR, или который запустил плагин, чтобы GUI прицепился к этой консоли");
    ConPrc.IsCmd = lstrcmpi(ConPrc.Name, _T("cmd.exe"))==0 || lstrcmpi(ConPrc.Name, _T("conemuc.exe"))==0;

    ConPrc.NameChecked = true;
}


 
BOOL CRealConsole::RetrieveConsoleInfo(BOOL bShortOnly)
{
    TODO("!!! WinEvent нужно перенести в ConEmu. Наиболее частую операцию по изменению курсора можно обрабатывать без участия ConEmuC");

    TODO("!!! Необходимо сделать флажок, для выбора между CECMD_GETFULLINFO/CECMD_GETSHORTINFO");

    BOOL lbRc = FALSE;
    HANDLE hPipe = NULL; 
    CESERVER_REQ in={0}, *pOut=NULL;
    BYTE cbReadBuf[512];
    BOOL fSuccess;
    DWORD cbRead, dwMode, dwErr;

    gSet.Performance(tPerfRead, FALSE);
    //#ifdef _DEBUG
    //DWORD dwStartTick = GetTickCount(), dwEndTick, dwDelta;
    //DEBUGSTR(L"CRealConsole::RetrieveConsoleInfo");
    //#endif

    // Try to open a named pipe; wait for it, if necessary. 
    while (1) 
    { 
      hPipe = CreateFile( 
         ms_ConEmuC_Pipe,// pipe name 
         GENERIC_READ |  // read and write access 
         GENERIC_WRITE, 
         0,              // no sharing 
         NULL,           // default security attributes
         OPEN_EXISTING,  // opens existing pipe 
         0,              // default attributes 
         NULL);          // no template file 

      // Break if the pipe handle is valid. 
      if (hPipe != INVALID_HANDLE_VALUE) 
         break; 

      // Exit if an error other than ERROR_PIPE_BUSY occurs. 
      dwErr = GetLastError();
      if (dwErr != ERROR_PIPE_BUSY) 
      {
        TODO("Подождать, пока появится пайп с таким именем, но только пока жив mh_ConEmuC");
        dwErr = WaitForSingleObject(mh_ConEmuC, 100);
        if (dwErr = WAIT_OBJECT_0)
            return FALSE;
        continue;
        //DisplayLastError(L"Could not open pipe", dwErr);
        //return 0;
      }

      // All pipe instances are busy, so wait for 1 second.
      if (!WaitNamedPipe(ms_ConEmuC_Pipe, 1000) ) 
      {
        dwErr = WaitForSingleObject(mh_ConEmuC, 100);
        if (dwErr = WAIT_OBJECT_0) {
            DEBUGSTR(L" - FAILED!\n");
            return FALSE;
        }
        //DisplayLastError(L"WaitNamedPipe failed"); 
        //return 0;
      }
    } 

    // The pipe connected; change to message-read mode. 
    dwMode = PIPE_READMODE_MESSAGE; 
    fSuccess = SetNamedPipeHandleState( 
      hPipe,    // pipe handle 
      &dwMode,  // new pipe mode 
      NULL,     // don't set maximum bytes 
      NULL);    // don't set maximum time 
    if (!fSuccess) 
    {
      DEBUGSTR(L" - FAILED!\n");
      DisplayLastError(L"SetNamedPipeHandleState failed");
      CloseHandle(hPipe);
      return 0;
    }

    in.nSize = 12;
    in.nVersion = CESERVER_REQ_VER;
    in.nCmd  = bShortOnly ? CECMD_GETSHORTINFO : CECMD_GETFULLINFO;

    // Send a message to the pipe server and read the response. 
    fSuccess = TransactNamedPipe( 
      hPipe,                  // pipe handle 
      &in,                    // message to server
      in.nSize,               // message length 
      cbReadBuf,              // buffer to receive reply
      sizeof(cbReadBuf),      // size of read buffer
      &cbRead,                // bytes read
      NULL);                  // not overlapped 

    if (!fSuccess && (GetLastError() != ERROR_MORE_DATA)) 
    {
      DEBUGSTR(L" - FAILED!\n");
      DisplayLastError(L"TransactNamedPipe failed"); 
      CloseHandle(hPipe);
      return 0;
    }

    int nAllSize = *((DWORD*)cbReadBuf);
    if (nAllSize==0) {
       DEBUGSTR(L" - FAILED!\n");
       DisplayLastError(L"Empty data recieved from server", 0);
       CloseHandle(hPipe);
       return 0;
    }
    pOut = (CESERVER_REQ*)calloc(nAllSize,1);
    _ASSERTE(pOut!=NULL);
    memmove(pOut, cbReadBuf, cbRead);
    _ASSERTE(pOut->nVersion==CESERVER_REQ_VER);

    LPBYTE ptrData = ((LPBYTE)pOut)+cbRead;
    nAllSize -= cbRead;

    while(nAllSize>0)
    { 
      //_tprintf(TEXT("%s\n"), chReadBuf);

      // Break if TransactNamedPipe or ReadFile is successful
      if(fSuccess)
         break;

      // Read from the pipe if there is more data in the message.
      fSuccess = ReadFile( 
         hPipe,      // pipe handle 
         ptrData,    // buffer to receive reply 
         nAllSize,   // size of buffer 
         &cbRead,    // number of bytes read 
         NULL);      // not overlapped 

      // Exit if an error other than ERROR_MORE_DATA occurs.
      if( !fSuccess && (GetLastError() != ERROR_MORE_DATA)) 
         break;
      ptrData += cbRead;
      nAllSize -= cbRead;
    }

    TODO("Может возникнуть ASSERT, если консоль была закрыта в процессе чтения");
    _ASSERTE(nAllSize==0);

    CloseHandle(hPipe);

    ApplyConsoleInfo ( pOut );

    //// Теперь нужно открыть секцию - начинаем изменение переменных класса
    //CSection SCON(&csCON, &ncsTCON);
    //// Теперь нужно раскидать данные про структурам
    //LPBYTE lpCur = (LPBYTE)pOut->Data;
    //// 1
    //HWND hWnd = *((HWND*)lpCur);
    //_ASSERTE(hWnd!=NULL);
    //if (hConWnd != hWnd) {
    //  hConWnd = hWnd;
    //  InitHandlers(TRUE);
    //}
    //lpCur += sizeof(hWnd);
    //// 2
    //// во время чтения содержимого консоли может увеличиться количество процессов
    //// поэтому РЕАЛЬНОЕ количество - выставим после чтения и CriticalSection(csProc);
    ////CSection SPRC(&csPRC, &ncsTPRC);
    ////TODO("Сделать корректную обработку! не чистить m_Processes не глядя, а удалять только отсутствующие! также получить информацию о процессе");
    //int dwProcCount = (int)*((DWORD*)lpCur); lpCur += sizeof(DWORD);
    //if (dwProcCount) lpCur += sizeof(DWORD)*dwProcCount;
    ////m_Processes.clear();
    ////ConProcess prc = {0};
    ////while (dwProcCount>0) {
    ////    prc.ProcessID = *((DWORD*)lpCur); lpCur += sizeof(DWORD);
    ////    m_Processes.push_back(prc);
    ////    dwProcCount--;
    ////}
    ////SPRC.Leave();
    //// 3
    //DWORD dwSelRc = *((DWORD*)lpCur); lpCur += sizeof(DWORD); //CONSOLE_SELECTION_INFO sel = {0}; // GetConsoleSelectionInfo
    //memmove(&m_sel, lpCur, sizeof(m_sel));
    //lpCur += sizeof(m_sel);
    //// 4
    //DWORD dwCiRc = *((DWORD*)lpCur); lpCur += sizeof(DWORD); //CONSOLE_CURSOR_INFO ci = {0}; // GetConsoleCursorInfo
    //memmove(&m_ci, lpCur, sizeof(m_ci));
    //lpCur += sizeof(cinf);
    //// 5, 6, 7
    //m_dwConsoleCP = *((DWORD*)lpCur); lpCur += sizeof(DWORD);       // GetConsoleCP()
    //m_dwConsoleOutputCP = *((DWORD*)lpCur); lpCur += sizeof(DWORD); // GetConsoleOutputCP()
    //m_dwConsoleMode = *((DWORD*)lpCur); lpCur += sizeof(DWORD);     // GetConsoleMode(hConIn, &dwConsoleMode);
    //// 8
    //DWORD dwSbiRc = *((DWORD*)lpCur); lpCur += sizeof(DWORD); //CONSOLE_SCREEN_BUFFER_INFO sbi = {{0,0}}; // GetConsoleScreenBufferInfo
    //memmove(&m_sbi, lpCur, sizeof(csbi));
    //lpCur += sizeof(csbi);
    //// 9
    //if (!bShortOnly) {
    //  DWORD OneBufferSize = *((DWORD*)lpCur); lpCur += sizeof(DWORD);
    //  TODO("Скопировать содержимое буферов, если OneBufferSize>0");
    //  if (InitBuffers()) {
    //      memmove(ConChar, lpCur, OneBufferSize); lpCur += OneBufferSize;
    //      memmove(ConAttr, lpCur, OneBufferSize); lpCur += OneBufferSize;
    //  }
    //}

    // Освободить память
    free(pOut);

    //#ifdef _DEBUG
    //dwEndTick = GetTickCount();
    //dwDelta = dwEndTick - dwStartTick;
    //WCHAR szText[64]; wsprintfW(szText, L" - SUCCEEDED (%i ms)!\n", dwDelta);
    //DEBUGSTR(szText);
    //#endif
    gSet.Performance(tPerfRead, TRUE);
    return TRUE;
}

BOOL CRealConsole::GetConWindowSize(const CONSOLE_SCREEN_BUFFER_INFO& sbi, int& nNewWidth, int& nNewHeight, BOOL* pbBufferHeight/*=NULL*/)
{
	// Функция возвращает размер ОКНА, то есть буфер может быть больше
	WARNING("проверить, может там где вызывается GetConWindowSize нужно включить режим BufferHeight?");

    nNewWidth  = sbi.dwSize.X; // sbi.srWindow.Right - sbi.srWindow.Left + 1;
	BOOL lbBufferHeight = con.bBufferHeight;
	// Проверка режимов прокрутки
	if (!lbBufferHeight) {
		if (sbi.dwSize.Y > sbi.dwMaximumWindowSize.Y)
		{
			lbBufferHeight = TRUE; // однозначное включение прокрутки
		}
	}
	if (lbBufferHeight) {
		if (sbi.srWindow.Top == 0
			&& sbi.dwSize.Y == (sbi.srWindow.Bottom + 1)
			)
		{
			lbBufferHeight = FALSE; // однозначное вЫключение прокрутки
		}
	}

	// Теперь собственно размеры
    if (!lbBufferHeight) {
        nNewHeight =  sbi.dwSize.Y;
    } else {
        // Это может прийти во время смены размера
        if ((sbi.srWindow.Bottom - sbi.srWindow.Top + 1) < MIN_CON_HEIGHT)
            nNewHeight = con.nTextHeight;
        else
            nNewHeight = sbi.srWindow.Bottom - sbi.srWindow.Top + 1;
    }
	WARNING("Здесь нужно выполнить коррекцию, если nNewHeight велико - включить режим BufferHeight");


    _ASSERTE(nNewWidth>=MIN_CON_WIDTH && nNewHeight>=MIN_CON_HEIGHT);

    if (!nNewWidth || !nNewHeight) {
        Assert(nNewWidth && nNewHeight);
        return FALSE;
    }

    //if (nNewWidth < sbi.dwSize.X)
    //    nNewWidth = sbi.dwSize.X;

    return TRUE;
}

BOOL CRealConsole::InitBuffers(DWORD OneBufferSize)
{
    BOOL lbRc = FALSE;
    int nNewWidth = 0, nNewHeight = 0;

    if (!GetConWindowSize(con.m_sbi, nNewWidth, nNewHeight))
        return FALSE;

    if (OneBufferSize) {
		if ((nNewWidth * nNewHeight * sizeof(*con.pConChar)) != OneBufferSize) {
			_ASSERTE((nNewWidth * nNewHeight * sizeof(*con.pConChar)) == OneBufferSize);
		}
        if ((nNewWidth * nNewHeight * sizeof(*con.pConChar)) != OneBufferSize)
            return FALSE;
    }

    // Если требуется увеличить или создать (первично) буфера
    if (!con.pConChar || (con.nTextWidth*con.nTextHeight) < (nNewWidth*nNewHeight))
    {
        CSection sc(&con.cs, &con.ncsT);

        try {
            if (con.pConChar)
                { Free(con.pConChar); con.pConChar = NULL; }
            if (con.pConAttr)
                { Free(con.pConAttr); con.pConAttr = NULL; }

            con.pConChar = (TCHAR*)Alloc((nNewWidth * nNewHeight * 2), sizeof(*con.pConChar));
            con.pConAttr = (WORD*)Alloc((nNewWidth * nNewHeight * 2), sizeof(*con.pConAttr));
        } catch(...) {
            con.pConChar = NULL;
            con.pConAttr = NULL;
        }

        sc.Leave();

        _ASSERTE(con.pConChar!=NULL);
        _ASSERTE(con.pConAttr!=NULL);

        MCHKHEAP

        lbRc = con.pConChar!=NULL && con.pConAttr!=NULL;
    } else if (con.nTextWidth!=nNewWidth || con.nTextHeight!=nNewHeight) {
        memset(con.pConChar, 0, (nNewWidth * nNewHeight * 2) * sizeof(*con.pConChar));
        memset(con.pConAttr, 0, (nNewWidth * nNewHeight * 2) * sizeof(*con.pConAttr));

        lbRc = TRUE;
    } else {
        lbRc = TRUE;
    }
    MCHKHEAP

    con.nTextWidth = nNewWidth;
    con.nTextHeight = nNewHeight;

    //InitDC(false,true);

    return lbRc;
}

void CRealConsole::ShowConsole(int nMode) // -1 Toggle, 0 - Hide, 1 - Show
{
    if (this == NULL) return;
    if (!hConWnd) return;
    
    if (nMode == -1) {
        nMode = IsWindowVisible(hConWnd) ? 0 : 1;
    }
    
    if (nMode == 1)
    {
        isShowConsole = true;
        //ShowWindow(hConWnd, SW_SHOWNORMAL);
        //if (setParent) SetParent(hConWnd, 0);
        RECT rcCon, rcWnd; GetWindowRect(hConWnd, &rcCon); GetWindowRect(ghWnd, &rcWnd);
        /* */ SetWindowPos(hConWnd, HWND_TOPMOST, 
            rcWnd.right-rcCon.right+rcCon.left, rcWnd.bottom-rcCon.bottom+rcCon.top,
            0,0, SWP_NOSIZE|SWP_SHOWWINDOW);
        EnableWindow(hConWnd, true);
        SetFocus(ghWnd);
        //if (setParent) SetParent(hConWnd, 0);
    }
    else
    {
        isShowConsole = false;
        //if (!gSet.isConVisible)
        ShowWindow(hConWnd, SW_HIDE);
        //if (setParent) SetParent(hConWnd, setParent2 ? ghWnd : ghWndDC);
        //if (!gSet.isConVisible)
        //EnableWindow(hConWnd, false); -- наверное не нужно
        SetFocus(ghWnd);
    }
}

void CRealConsole::SetHwnd(HWND ahConWnd)
{
    hConWnd = ahConWnd;

    if (ms_VConServer_Pipe[0] == 0) {
        // временно используем эту переменную, чтобы не плодить локальных
        wsprintfW(ms_VConServer_Pipe, CEGUIATTACHED, (DWORD)hConWnd);
        mh_GuiAttached = CreateEvent(NULL, TRUE, FALSE, ms_VConServer_Pipe);
        _ASSERTE(mh_GuiAttached!=NULL);

        // Запустить серверный пайп
        wsprintf(ms_VConServer_Pipe, CEGUIPIPENAME, L".", (DWORD)hConWnd); //был mn_ConEmuC_PID
        mh_ServerSemaphore = CreateSemaphore(NULL, 1, 1, NULL);
        for (int i=0; i<MAX_SERVER_THREADS; i++) {
            mn_ServerThreadsId[i] = 0;
            mh_ServerThreads[i] = CreateThread(NULL, 0, ServerThread, (LPVOID)this, 0, &mn_ServerThreadsId[i]);
            _ASSERTE(mh_ServerThreads[i]!=NULL);
        }

        // чтобы ConEmuC знал, что мы готовы
        SetEvent(mh_GuiAttached);
    }

    if (gSet.isConVisible)
        ShowConsole(1); // установить консольному окну флаг AlwaysOnTop

	if (gConEmu.isActive(mp_VCon)) {
		ghConWnd = hConWnd;
		// Чтобы можно было найти хэндл окна по хэндлу консоли
		SetWindowLong(ghWnd, GWL_USERDATA, (LONG)hConWnd);
	}
}

void CRealConsole::OnFocus(BOOL abFocused)
{
    if (!this) return;

    if ((mn_Focused == -1) ||
        ((mn_Focused == 0) && abFocused) ||
        ((mn_Focused == 1) && !abFocused))
    {
#ifdef _DEBUG
        if (abFocused)
            DEBUGSTR(L"--Get focus\n");
        else
            DEBUGSTR(L"--Loose focus\n");
#endif
        INPUT_RECORD r = {FOCUS_EVENT};
        r.Event.FocusEvent.bSetFocus = abFocused;
        SendConsoleEvent(&r);

        mn_Focused = abFocused ? 1 : 0;
    }
}

void CRealConsole::CreateLogFiles()
{
	if (!mb_UseLogs || mh_LogInput) return; // уже

	DWORD dwErr = 0;
	wchar_t szFile[MAX_PATH+64], *pszDot;
	if (!GetModuleFileName(NULL, szFile, MAX_PATH)) {
		dwErr = GetLastError();
		DisplayLastError(L"GetModuleFileName failed! ErrCode=0x%08X\n", dwErr);
		return; // не удалось
	}
	if ((pszDot = wcsrchr(szFile, L'\\')) == NULL) {
		DisplayLastError(L"wcsrchr failed!");
		return; // ошибка
	}
	*pszDot = 0;

	mpsz_LogPackets = (wchar_t*)calloc(pszDot - szFile + 64, 2);
	wcscpy(mpsz_LogPackets, szFile);
	wsprintf(mpsz_LogPackets+wcslen(mpsz_LogPackets), L"\\ConEmu-recv-%i-%%i.con", mn_ConEmuC_PID); // ConEmu-recv-<ConEmuC_PID>-<index>.con

	wsprintfW(pszDot, L"\\ConEmu-input-%i.log", mn_ConEmuC_PID);
	

	mh_LogInput = CreateFileW ( szFile, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (mh_LogInput == INVALID_HANDLE_VALUE) {
		mh_LogInput = NULL;
		dwErr = GetLastError();
		wprintf(L"CreateFile failed! ErrCode=0x%08X\n%s\n", dwErr, szFile);
		return;
	}

	mpsz_LogInputFile = wcsdup(szFile);
	// OK, лог создали
}

void CRealConsole::LogInput(INPUT_RECORD* pRec)
{
	if (!mh_LogInput) return;

	char szInfo[192] = {0};

	SYSTEMTIME st; GetLocalTime(&st);
	sprintf(szInfo, "%i:%02i:%02i.%03i ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	char *pszAdd = szInfo+strlen(szInfo);

	switch (pRec->EventType) {
	/*case FOCUS_EVENT: sprintf(pszAdd, "FOCUS_EVENT(%i)\r\n", pRec->Event.FocusEvent.bSetFocus);
		break;*/
	/*case MOUSE_EVENT: sprintf(pszAdd, "MOUSE_EVENT\r\n");
		break;*/
	case KEY_EVENT:
		{
			char chAcp; WideCharToMultiByte(CP_ACP, 0, &pRec->Event.KeyEvent.uChar.UnicodeChar, 1, &chAcp, 1, 0,0);
			/* */ sprintf(pszAdd, "%c %s count=%i, VK=%i, SC=%i, CH=%i, State=0x%08x %s\r\n",
				chAcp ? chAcp : ' ',
				pRec->Event.KeyEvent.bKeyDown ? "Down," : "Up,  ",
				pRec->Event.KeyEvent.wRepeatCount,
				pRec->Event.KeyEvent.wVirtualKeyCode,
				pRec->Event.KeyEvent.wVirtualScanCode,
				pRec->Event.KeyEvent.uChar.UnicodeChar,
				pRec->Event.KeyEvent.dwControlKeyState,
				(pRec->Event.KeyEvent.dwControlKeyState & ENHANCED_KEY) ?
				"<Enhanced>" : "");
		} break;
	}

	if (*pszAdd) {
		DWORD dwLen = 0;
		WriteFile(mh_LogInput, szInfo, strlen(szInfo), &dwLen, 0);
		FlushFileBuffers(mh_LogInput);
	}
}

void CRealConsole::CloseLogFiles()
{
	SafeCloseHandle(mh_LogInput);
	if (mpsz_LogInputFile) {
		DeleteFile(mpsz_LogInputFile);
		free(mpsz_LogInputFile); mpsz_LogInputFile = NULL;
	}
	if (mpsz_LogPackets) {
		wchar_t szMask[MAX_PATH*2]; wcscpy(szMask, mpsz_LogPackets);
		wchar_t *psz = wcsrchr(szMask, L'%');
		if (psz) {
			wcscpy(psz, L"*.*");
			psz = wcsrchr(szMask, L'\\'); 
			if (psz) {
				psz++;
				WIN32_FIND_DATA fnd;
				HANDLE hFind = FindFirstFile(szMask, &fnd);
				if (hFind != INVALID_HANDLE_VALUE) {
					do {
						if ((fnd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)==0) {
							wcscpy(psz, fnd.cFileName);
							DeleteFile(szMask);
						}
					} while (FindNextFile(hFind, &fnd));
					FindClose(hFind);
				}
			}
		}
		free(mpsz_LogPackets); mpsz_LogPackets = NULL;
	}
}

void CRealConsole::LogPacket(CESERVER_REQ* pInfo)
{
	if (!mpsz_LogPackets) return;

	wchar_t szPath[MAX_PATH];
	wsprintf(szPath, mpsz_LogPackets, ++mn_LogPackets);

	HANDLE hFile = CreateFile(szPath, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	if (hFile != INVALID_HANDLE_VALUE) {
		DWORD dwSize = 0;
		WriteFile(hFile, pInfo, pInfo->nSize, &dwSize, 0);
		CloseHandle(hFile);
	}
}



// Послать в консоль запрос на закрытие
BOOL CRealConsole::RecreateProcess()
{
	if (!this)
		return false;
		
    _ASSERTE(hConWnd && mh_ConEmuC);

	if (!hConWnd || !mh_ConEmuC) {
        Box(_T("Console was not created (CRealConsole::SetConsoleSize)"));
        return false; // консоль пока не создана?
	}
	if (mn_InRecreate) {
        Box(_T("Console already in recreate..."));
        return false;
	}

	DWORD nWait = 0;

	mb_ProcessRestarted = FALSE;
	mn_InRecreate = GetTickCount();
	if (!mn_InRecreate) {
		DisplayLastError(L"GetTickCount failed");
		return false;
	}

	SENDMESSAGE(hConWnd, WM_CLOSE, 0, 0);

	return true;

	// как-то оно не особо... лучше штатными средствами
#ifdef xxxxxx
    bool lbRc = false;
    BOOL fSuccess = FALSE;
    DWORD dwRead = 0;
    CESERVER_REQ In = {0};
	HANDLE hPipe = NULL;
	DWORD dwErr = 0;

    In.nSize = sizeof(In);
    In.nVersion = CESERVER_REQ_VER;
    In.nCmd = CECMD_RECREATE;


    //fSuccess = CallNamedPipe(ms_ConEmuC_Pipe, pIn, pIn->nSize, pOut, pOut->nSize, &dwRead, 500);

    // Try to open a named pipe; wait for it, if necessary. 
    while (1) 
    { 
      hPipe = CreateFile( ms_ConEmuC_Pipe, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
      // Break if the pipe handle is valid. 
      if (hPipe != INVALID_HANDLE_VALUE) 
         break; 
      // Exit if an error other than ERROR_PIPE_BUSY occurs. 
      dwErr = GetLastError();
      if (dwErr != ERROR_PIPE_BUSY) 
      {
        TODO("Подождать, пока появится пайп с таким именем, но только пока жив mh_ConEmuC");
        dwErr = WaitForSingleObject(mh_ConEmuC, 100);
        if (dwErr = WAIT_OBJECT_0)
            return FALSE;
        continue;
      }

      // All pipe instances are busy, so wait for 1 second.
      if (!WaitNamedPipe(ms_ConEmuC_Pipe, 300) ) 
      {
        dwErr = WaitForSingleObject(mh_ConEmuC, 100);
        if (dwErr = WAIT_OBJECT_0) {
            DEBUGSTR(L" - FAILED!\n");
            return false;
        }
      }
    } 

    // The pipe connected; change to message-read mode. 
    DWORD dwMode = PIPE_READMODE_MESSAGE; 
    fSuccess = SetNamedPipeHandleState( 
      hPipe,    // pipe handle 
      &dwMode,  // new pipe mode 
      NULL,     // don't set maximum bytes 
      NULL);    // don't set maximum time 
    if (!fSuccess) 
    {
      DEBUGSTR(L" - FAILED!\n");
      DisplayLastError(L"SetNamedPipeHandleState failed");
      CloseHandle(hPipe);
      return false;
    }
    
    mn_InRecreate = 1;

    // Send a message to the pipe server and read the response. 
    DWORD cbWritten=0;
    lbRc = WriteFile( hPipe, &In, In.nSize, &cbWritten, NULL);
    
    SafeCloseHandle ( hPipe );

    return lbRc;
#endif
}

// И запустить ее заново
BOOL CRealConsole::RecreateProcessStart()
{
	bool lbRc = false;
	if (mn_InRecreate && mn_ProcessCount == 0 && !mb_ProcessRestarted) {
		mb_ProcessRestarted = TRUE;
	    
		StopThread(TRUE/*abRecreate*/);
	    
		ResetEvent(mh_TermEvent);
		hConWnd = NULL;
		ms_VConServer_Pipe[0] = 0;
		SafeCloseHandle(mh_ServerSemaphore);
		SafeCloseHandle(mh_GuiAttached);

		if (!StartProcess()) {
			mn_InRecreate = 0;
			mb_ProcessRestarted = FALSE;
		    
		} else {
			ResetEvent(mh_TermEvent);
			lbRc = StartMonitorThread();
		}
	}
	return lbRc;
}

void CRealConsole::GetData(wchar_t* pChar, WORD* pAttr, uint nSize)
{
	TODO("Проверить, что размер con.pCon(Char|Attr) не меньше nSize");
    CSection csData(&con.cs, &con.ncsT);
    HEAPVAL
    memmove(pChar, con.pConChar, nSize);
    memmove(pAttr, con.pConAttr, nSize);
    HEAPVAL
    csData.Leave();
}

void CRealConsole::OnActivate(int nNewNum, int nOldNum)
{
	_ASSERTE(gConEmu.isActive(mp_VCon));

	// Чтобы можно было найти хэндл окна по хэндлу консоли
	SetWindowLong(ghWnd, GWL_USERDATA, (LONG)hConWnd);
	ghConWnd = hConWnd;

	WARNING("Не работало обновление заголовка");
	gConEmu.UpdateTitle(Title);

	UpdateScrollInfo();

	TabBar.OnConman(nNewNum+1, FALSE);
	TabBar.Update();

	gConEmu.UpdateProcessDisplay(TRUE);
}

BOOL CRealConsole::BufferHeightTurnedOn(CONSOLE_SCREEN_BUFFER_INFO* psbi)
{
	BOOL lbTurnedOn = FALSE;
	TODO("!!! Скорректировать");
	if (psbi->dwSize.Y == (psbi->srWindow.Bottom - psbi->srWindow.Top + 1)) {
		// высота окна == высоте буфера, 
		lbTurnedOn = FALSE;
	} else if (con.m_sbi.dwSize.Y < (con.m_sbi.srWindow.Bottom-con.m_sbi.srWindow.Top+10)) {
		// Высота окна примерно равна высоте буфера
		lbTurnedOn = FALSE;
	} else if (con.m_sbi.dwSize.Y>(con.m_sbi.srWindow.Bottom-con.m_sbi.srWindow.Top+10)) {
		// Высота буфера 'намного' больше высоты окна
		lbTurnedOn = TRUE;
	}

	// однако, если высота слишком велика для отображения в GUI окне - нужно включить BufferHeight
	if (!lbTurnedOn) {
		//TODO: однако, если высота слишком велика для отображения в GUI окне - нужно включить BufferHeight
	}

	return lbTurnedOn;
}

void CRealConsole::UpdateScrollInfo()
{
	if (!gConEmu.isActive(mp_VCon))
		return;

	if (!gConEmu.isMainThread()) {
		
		return;
	}

	TODO("Как-то кэшировать нужно вызовы что-ли... и SetScrollInfo можно бы перенести в m_Back");

	int nCurPos = 0;
	BOOL lbScrollRc = FALSE;
	SCROLLINFO si;
	ZeroMemory(&si, sizeof(si));
	si.cbSize = sizeof(si);
	si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE | SIF_TRACKPOS;

	// Если режим "BufferHeight" включен - получить из консольного окна текущее состояние полосы прокрутки
	if (con.bBufferHeight) {
		lbScrollRc = GetScrollInfo(hConWnd, SB_VERT, &si);
	} else {
		// Сбросываем параметры так, чтобы полоса не отображалась (все на 0)
	}

	nCurPos = SetScrollInfo(gConEmu.m_Back.mh_Wnd, SB_VERT, &si, true);
}

WARNING("Проверить, что она делает...");
BOOL CRealConsole::CheckBufferSize()
{
	bool lbForceUpdate = false;

	if (!this)
		return false;


	//if (con.m_sbi.dwSize.X>(con.m_sbi.srWindow.Right-con.m_sbi.srWindow.Left+1)) {
	//	DEBUGLOGFILE("Wrong screen buffer width\n");
	//	// Окошко консоли почему-то схлопнулось по горизонтали
	//	WARNING("пока убрал для теста");
	//	//MOVEWINDOW(hConWnd, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), 1);
	//} else {

	// BufferHeight может меняться и из плагина, во время работы фара...
	BOOL lbTurnedOn = BufferHeightTurnedOn(&con.m_sbi);

	if ( !lbTurnedOn && con.bBufferHeight )
	{
		// может быть консольная программа увеличила буфер самостоятельно?
		// TODO: отключить прокрутку!!!
		con.bBufferHeight = FALSE;

		UpdateScrollInfo();

		lbForceUpdate = true;
	} 
	else if ( lbTurnedOn && !con.bBufferHeight )
	{
		con.bBufferHeight = TRUE;

		UpdateScrollInfo();

		lbForceUpdate = true;
	}

	//TODO: А если высота буфера вдруг сменилась из самой консольной программы?

	//if ((BufferHeight == 0) && (con.m_sbi.dwSize.Y>(con.m_sbi.srWindow.Bottom-con.m_sbi.srWindow.Top+1))) {
	//	TODO("это может быть консольная программа увеличила буфер самостоятельно!")
	//		DEBUGLOGFILE("Wrong screen buffer height\n");
	//	// Окошко консоли почему-то схлопнулось по вертикали
	//	WARNING("пока убрал для теста");
	//	//MOVEWINDOW(hConWnd, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), 1);
	//}

	//TODO: Можно бы перенести в ConEmuC, если нужно будет
	//// При выходе из FAR -> CMD с BufferHeight - смена QuickEdit режима
	//DWORD mode = 0;
	//BOOL lb = FALSE;
	//if (BufferHeight) {
	//	//TODO: похоже, что для BufferHeight это вызывается постоянно?
	//	//lb = GetConsoleMode(hConIn(), &mode);
	//	mode = GetConsoleMode();

	//	if (con.m_sbi.dwSize.Y>(con.m_sbi.srWindow.Bottom-con.m_sbi.srWindow.Top+1)) {
	//		// Буфер больше высоты окна
	//		mode |= ENABLE_QUICK_EDIT_MODE|ENABLE_INSERT_MODE|ENABLE_EXTENDED_FLAGS;
	//	} else {
	//		// Буфер равен высоте окна (значит ФАР запустился)
	//		mode &= ~(ENABLE_QUICK_EDIT_MODE|ENABLE_INSERT_MODE);
	//		mode |= ENABLE_EXTENDED_FLAGS;
	//	}

	//	TODO("SetConsoleMode");
	//	//lb = SetConsoleMode(hConIn(), mode);
	//}

	return lbForceUpdate;
}

//// Должен вернуть: HIWORD==newBufferHeight, LOWORD=newBufferWidth
//LRESULT CRealConsole::OnConEmuCmd(BOOL abStarted, DWORD anConEmuC_PID)
//{
//	WORD newBufferHeight = TextHeight();
//	WORD newBufferWidth  = TextWidth();
//
//	if (abStarted) {
//		ProcessAdd(anConEmuC_PID);
//
//		BufferHeight = TextHeight(); TODO("нужно вернуть реальный требуемый размер...");
//	} else {
//		ProcessDelete(anConEmuC_PID);
//
//		if ((mn_ActiveStatus & CES_CMDACTIVE) == 0)
//			BufferHeight = 0;
//	}
//
//	return MAKELONG(newBufferWidth,newBufferHeight);
//}


void CRealConsole::SetTabs(ConEmuTab* tabs, int tabsCount)
{
	ConEmuTab* lpNewTabs = NULL;
	
	int nActiveTab = 0;
	if (tabs && tabsCount) {
		int nNewSize = sizeof(ConEmuTab)*tabsCount;
		lpNewTabs = (ConEmuTab*)Alloc(nNewSize, 1);
		if (!lpNewTabs)
			return;
		memmove ( lpNewTabs, tabs, nNewSize );

		// иначе вместо "Panels" будет то что в заголовке консоли. Например "edit - doc1.doc"
		if (tabsCount > 1 && lpNewTabs[0].Type == 1/*WTYPE_PANELS*/ && lpNewTabs[0].Current)
			lpNewTabs[0].Name[0] = 0;

		if (tabsCount == 1)
			lpNewTabs[0].Current = 1;
		
		for (int i=(tabsCount-1); i>=0; i--) {
			if (lpNewTabs[i].Current) {
				nActiveTab = i; break;
			}
		}
		#ifdef _DEBUG
		for (int i=1; i<tabsCount; i++) {
			if (lpNewTabs[i].Name[0] == 0) {
				_ASSERTE(lpNewTabs[i].Name[0]!=0);
				//wcscpy(lpNewTabs[i].Name, L"ConEmu");
			}
		}
		#endif

	} else if (tabsCount == 1 && tabs == NULL) {
		lpNewTabs = (ConEmuTab*)Alloc(sizeof(ConEmuTab),1);
		if (!lpNewTabs)
			return;
        lpNewTabs->Pos = 0;
        lpNewTabs->Current = 1;
        lpNewTabs->Type = 1;
		//wcscpy(lpNewTabs->Name, L"ConEmu");
	}
	
	mn_tabsCount = 0; Free(mp_tabs); mn_ActiveTab = nActiveTab;
	mp_tabs = lpNewTabs; mn_tabsCount = tabsCount;

	if (mp_tabs && mn_tabsCount) {
		CheckPanelTitle();
	}
	
	// Передернуть TabBar...
	if (gConEmu.isValid(mp_VCon)) // Во время создания консоли она еще не добавлена в список...
		TabBar.Update();
}

// Если такого таба нет - pTab НЕ ОБНУЛЯТЬ!!!
BOOL CRealConsole::GetTab(int tabIdx, /*OUT*/ ConEmuTab* pTab)
{
	if (!this)
		return FALSE;
		
	if (!mp_tabs || tabIdx<0 || tabIdx>=mn_tabsCount)
		return FALSE;
	
	memmove(pTab, mp_tabs+tabIdx, sizeof(ConEmuTab));
	if (pTab->Name[0] == 0) {
		if (ms_PanelTitle[0]) { // скорее всего - это Panels?
			int nMaxLen = max(countof(ms_PanelTitle) , countof(pTab->Name));
			lstrcpyn(pTab->Name, ms_PanelTitle, nMaxLen);
		} else if (mn_tabsCount == 1 && Title[0]) { // Если панель единственная - точно показываем заголовок консоли
			int nMaxLen = max(countof(Title) , countof(pTab->Name));
			lstrcpyn(pTab->Name, Title, nMaxLen);
		}
	}
	return TRUE;
}

int CRealConsole::GetTabCount()
{
	if (!this)
		return 0;
		
	return mn_tabsCount;
}

void CRealConsole::CheckPanelTitle()
{
	if (mp_tabs && mn_tabsCount) {
		if ((mn_ActiveTab >= 0 && mn_ActiveTab < mn_tabsCount) || mn_tabsCount == 1) {
			WCHAR szPanelTitle[CONEMUTABMAX];
			if (!GetWindowText(hConWnd, szPanelTitle, countof(ms_PanelTitle)-1))
				ms_PanelTitle[0] = 0;
			else if (szPanelTitle[0] == L'{' || szPanelTitle[0] == L'(')
				lstrcpy(ms_PanelTitle, szPanelTitle);
		}
	}
}

DWORD CRealConsole::CanActivateFarWindow(int anWndIndex)
{
	if (!this)
		return 0;
		
	DWORD dwPID = GetFarPID();
	if (!dwPID)
		return 0;

	// Если есть меню: "╔══════" (первая или вторая строка консоли) - выходим
	// Если идет процесс (в заголовке консоли {n%}) - выходим
	// Если висит диалог - выходим (диалог обработает сам плагин)
	
	if (anWndIndex<0 || anWndIndex>=mn_tabsCount)
		return 0;

	// Добавил такую проверочку. По идее, у нас всегда должен быть актуальный номер текущего окна.
	if (mn_ActiveTab == anWndIndex)
		return (DWORD)-1; // Нужное окно уже выделено, лучше не дергаться...

	if (!GetWindowText(hConWnd, TitleCmp, countof(TitleCmp)-2))
		TitleCmp[0] = 0;
	// Копирование в FAR: "{33%}..."
	//2009-06-02: PPCBrowser показывает копирование так: "(33% 00:02:20)..."
	if ((TitleCmp[0] == L'{' || TitleCmp[0] == L'(')
		&& isdigit(TitleCmp[1]) &&
		((TitleCmp[2] == L'%' /*&& TitleCmp[3] == L'}'*/) ||
		 (isdigit(TitleCmp[2]) && TitleCmp[3] == L'%' /*&& TitleCmp[4] == L'}'*/) ||
		 (isdigit(TitleCmp[2]) && isdigit(TitleCmp[3]) && TitleCmp[4] == L'%' /*&& TitleCmp[5] == L'}'*/))
	   )
	{
		// Идет копирование
		return 0;
	}
	if (!con.pConChar || !con.nTextWidth || con.nTextHeight<2)
		return 0; // консоль не инициализирована, ловить нечего
	BOOL lbMenuActive = FALSE;
	if (mp_tabs && mn_ActiveTab >= 0 && mn_ActiveTab < mn_tabsCount)
	{	// Меню может быть только в панелях
		if (mp_tabs[mn_ActiveTab].Type == 1/*WTYPE_PANELS*/)
		{
			CSection cs(&con.cs, &con.ncsT);
			if (con.pConChar) {
				TODO("Тут не получилось учесть ситуацию, когда меню показано всегда и фокус находится в нем. F11 - повиснет...");
				if (con.pConChar[0] == L' ' && con.pConChar[con.nTextWidth] == L'║') {
					lbMenuActive = TRUE;
				} else {
					wchar_t* pszLine = con.pConChar + con.nTextWidth;
					for (int x=1; !lbMenuActive && x<(con.nTextWidth-10); x++)
					{
						if (pszLine[x] == L'╔' && pszLine[x+1] == L'═')
							lbMenuActive = TRUE;
					}
				}
			}
			cs.Leave();
		}
	}
	
	// Если строка меню отображается всегда:
	//  0-строка начинается с "  " или с "R   " если идет запись макроса
	//  1-я строка "╔═══" или "[0+1]═══"
	//  2-я строка "║"
	// Наличие активного меню определяем по количеству цветов в первой строке.
	// Неактивное меню отображается всегда одним цветом - в активном подсвечиваются хоткеи и выбранный пункт
	
	if (lbMenuActive)
		return 0;
		
	return dwPID;
}

BOOL CRealConsole::ActivateFarWindow(int anWndIndex)
{
	if (!this)
		return FALSE;
	

	DWORD dwPID = CanActivateFarWindow(anWndIndex);
	if (!dwPID)
		return FALSE;
	else if (dwPID == (DWORD)-1)
		return TRUE; // Нужное окно уже выделено, лучше не дергаться...


	BOOL lbRc = FALSE;

	CConEmuPipe pipe(dwPID, 100);
	if (pipe.Init(_T("CRealConsole::ActivateFarWindow")))
	{
		DWORD cbWritten = 0;
		if (pipe.Execute(CMD_SETWINDOW, &anWndIndex, 4))
		{
			DWORD cbBytesRead=0;
			DWORD tabCount = 0;
			ConEmuTab* tabs = NULL;
			if (pipe.Read(&tabCount, sizeof(DWORD), &cbBytesRead)) {
				tabs = (ConEmuTab*)pipe.GetPtr(&cbBytesRead);
				_ASSERTE(cbBytesRead==(tabCount*sizeof(ConEmuTab)));
				if (cbBytesRead == (tabCount*sizeof(ConEmuTab))) {
					SetTabs(tabs, tabCount);
					lbRc = TRUE;
				}
			}

			pipe.Close();
		}
	}

	return lbRc;
}

BOOL CRealConsole::IsConsoleThread()
{
	if (!this) return FALSE;

	DWORD dwCurThreadId = GetCurrentThreadId();
	return dwCurThreadId == mn_MonitorThreadID;
}

void CRealConsole::SetForceRead()
{
	SetEvent(mh_ForceReadEvent);
}

DWORD CRealConsole::WaitEndUpdate(DWORD dwTimeout)
{
	DWORD dwWait = WaitForSingleObject(mh_EndUpdateEvent, dwTimeout);
	return dwWait;
}
