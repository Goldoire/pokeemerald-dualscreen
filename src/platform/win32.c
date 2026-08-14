#ifdef PLATFORM_WIN32
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include <windows.h>

#ifdef xinputkeys
#include <xinput.h>
#endif

#ifdef NO_STD_LIB_ENABLED
#define main _main
#endif

#include "global.h"
#include "platform.h"
#include "rtc.h"
#include "gba/defines.h"
#include "gba/m4a_internal.h"
#include "cgb_audio.h"
#include "gba/flash_internal.h"
#include "platform/dma.h"
#include "platform/framedraw.h"

extern void (*const gIntrTable[])(void);

HANDLE vBlankSemaphore;
bool isFrameAvailable;
bool speedUp = false;
unsigned int videoScale = 1;
bool videoScaleChanged = false;
bool isRunning = true;
bool paused = false;
double simTime = 0;
double lastGameTime = 0;
double curGameTime = 0;
double fixedTimestep = 1.0 / 60.0; // 16.666667ms
double timeScale = 1.0;
struct SiiRtcInfo internalClock;
char const* savePath = "pokeemerald.sav";
int frameSkipSet = 1;
int frameskipCounter = 0;
bool bitBltEnabled = true;

static HANDLE sSaveFile = NULL;

extern void AgbMain(void);
extern void DoSoftReset(void);

DWORD WINAPI DoMain(LPVOID lpParam);
void VDraw();

static void ReadSaveFile(char *path);
static void StoreSaveFile(void);
static void CloseSaveFile(void);
static void UpdateInternalClock(void);

HINSTANCE hInst;
char const* szTitle = "win32 emerald";
char const* szWindowClass = "pokeemerald";
HWND ghwnd;

static u16 keys;

#define IDM_SPEEDUPTOGGLE 1
#define IDM_SPEEDUPTOGGLETEXT "&Speed up game"
#define IDM_RESETGAME 2
#define IDM_RESETGAMETEXT "&Reset game"
#define IDM_PAUSEGAME 3
#define IDM_PAUSEGAMETEXT "&Pause game"

#define IDM_60FPS 4
#define IDM_30FPS 5
#define IDM_20FPS 6
#define IDM_10FPS 7
#define IDM_5FPS 8
#define IDM_3FPS 9
#define IDM_1FPS 10
#define IDM_60FPSTEXT "60 FPS"
#define IDM_30FPSTEXT "30 FPS"
#define IDM_20FPSTEXT "20 FPS"
#define IDM_10FPSTEXT "10 FPS"
#define IDM_5FPSTEXT "5 FPS"
#define IDM_3FPSTEXT "3 FPS"
#define IDM_1FPSTEXT "1 FPS"

#define IDM_TOGGLEBITBLT 11
#define IDM_TOGGLEBITBLTTEXT "&Toggle bitblt"

//no standard library workarounds, these have to be defined
#ifdef NO_STD_LIB_ENABLED
void __chkstk_ms()
{
    return;
}

void acrt_iob_func()
{
    return;
}

void (*_imp____acrt_iob_func)(void) = &acrt_iob_func;
#endif


void AddMenus(HWND hwnd) {

    HMENU hMenubar;
    HMENU hMenu;
    HMENU hMenuFps;

    hMenubar = CreateMenu();
    hMenu = CreateMenu();
    hMenuFps = CreateMenu();

    AppendMenuA(hMenu, MF_STRING, IDM_SPEEDUPTOGGLE, IDM_SPEEDUPTOGGLETEXT);
    AppendMenuA(hMenu, MF_STRING, IDM_RESETGAME, IDM_RESETGAMETEXT);
    AppendMenuA(hMenu, MF_STRING, IDM_PAUSEGAME, IDM_PAUSEGAMETEXT);
    AppendMenuA(hMenu, MF_STRING, IDM_TOGGLEBITBLT, IDM_TOGGLEBITBLTTEXT);
    
    AppendMenuA(hMenuFps, MF_STRING, IDM_60FPS, IDM_60FPSTEXT);
    AppendMenuA(hMenuFps, MF_STRING, IDM_30FPS, IDM_30FPSTEXT);
    AppendMenuA(hMenuFps, MF_STRING, IDM_20FPS, IDM_20FPSTEXT);
    AppendMenuA(hMenuFps, MF_STRING, IDM_10FPS, IDM_10FPSTEXT);
    AppendMenuA(hMenuFps, MF_STRING, IDM_5FPS, IDM_5FPSTEXT);
    AppendMenuA(hMenuFps, MF_STRING, IDM_3FPS, IDM_3FPSTEXT);
    AppendMenuA(hMenuFps, MF_STRING, IDM_1FPS, IDM_1FPSTEXT);


    AppendMenuA(hMenubar, MF_POPUP, (UINT_PTR) hMenu, "&Debug");
    AppendMenuA(hMenubar, MF_POPUP, (UINT_PTR) hMenuFps, "&FPS");
    SetMenu(hwnd, hMenubar);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int wmId, wmEvent;
    PAINTSTRUCT ps;
    HDC hdc;

    switch (message)
    {
    case WM_CREATE:
        AddMenus(hWnd);
        break;
    case WM_PAINT:
        hdc = BeginPaint(hWnd, &ps);
        //OnPaint(hdc);
        EndPaint(hWnd, &ps);
        break;
    case WM_DESTROY:
        ExitProcess(1);
        break;
    case WM_KEYDOWN:
        switch(wParam)
        {
            case 'Z':
                keys |= A_BUTTON;
                break;
            case 'X':
                keys |= B_BUTTON;
                break;
            case 'A':
                keys |= L_BUTTON;
                break;
            case 'S':
                keys |= R_BUTTON;
                break;
            case VK_UP:
                keys |= DPAD_UP;
                break;
            case VK_DOWN:
                keys |= DPAD_DOWN;
                break;
            case VK_RIGHT:
                keys |= DPAD_RIGHT;
                break;
            case VK_LEFT:
                keys |= DPAD_LEFT;
                break;
            case VK_RETURN:
                keys |= START_BUTTON;
                break;
            case VK_SHIFT:
                keys |= SELECT_BUTTON;
                break;
            
        }
        break;
    case WM_KEYUP:
        switch(wParam)
        {
            case 'Z':
                keys &= ~A_BUTTON;
                break;
            case 'X':
                keys &= ~B_BUTTON;
                break;
            case 'A':
                keys &= ~L_BUTTON;
                break;
            case 'S':
                keys &= ~R_BUTTON;
                break;
            case VK_UP:
                keys &= ~DPAD_UP;
                break;
            case VK_DOWN:
                keys &= ~DPAD_DOWN;
                break;
            case VK_RIGHT:
                keys &= ~DPAD_RIGHT;
                break;
            case VK_LEFT:
                keys &= ~DPAD_LEFT;
                break;
            case VK_RETURN:
                keys &= ~START_BUTTON;
                break;
            case VK_SHIFT:
                keys &= ~SELECT_BUTTON;
                break;
        }
        break;
        case WM_COMMAND:
        
        switch(LOWORD(wParam)) 
        {
        case IDM_SPEEDUPTOGGLE:
            if (!speedUp)
            {
                timeScale = 100.0;
                speedUp = true;
                ModifyMenuA(GetMenu(hWnd), IDM_SPEEDUPTOGGLE, MF_BYCOMMAND | MF_CHECKED, IDM_SPEEDUPTOGGLE, IDM_SPEEDUPTOGGLETEXT);
            }else{
                ModifyMenuA(GetMenu(hWnd), IDM_SPEEDUPTOGGLE, MF_BYCOMMAND | MF_UNCHECKED, IDM_SPEEDUPTOGGLE, IDM_SPEEDUPTOGGLETEXT);
                speedUp = false;
                timeScale = 1.0;
            }
            break;
        case IDM_RESETGAME:
            DoSoftReset();
            break;
        case IDM_PAUSEGAME:
            if (!paused){
                paused = true;
                ModifyMenuA(GetMenu(hWnd), IDM_PAUSEGAME, MF_BYCOMMAND | MF_CHECKED, IDM_PAUSEGAME, IDM_PAUSEGAMETEXT);
            }
            else{
                paused = false;
                ModifyMenuA(GetMenu(hWnd), IDM_PAUSEGAME, MF_BYCOMMAND | MF_UNCHECKED, IDM_PAUSEGAME, IDM_PAUSEGAMETEXT);
            }
            break;
        case IDM_TOGGLEBITBLT:
            bitBltEnabled = !bitBltEnabled;
            break;
        case IDM_60FPS:
            frameSkipSet = 1;
            frameskipCounter = 0;
            break;
        case IDM_30FPS:
            frameSkipSet = 2;
            frameskipCounter = 0;
            break;
        case IDM_20FPS:
            frameSkipSet = 3;
            frameskipCounter = 0;
            break;
        case IDM_10FPS:
            frameSkipSet = 6;
            frameskipCounter = 0;
            break;
        case IDM_5FPS:
            frameSkipSet = 12;
            frameskipCounter = 0;
            break;
        case IDM_3FPS:
            frameSkipSet = 20;
            frameskipCounter = 0;
            break;
        case IDM_1FPS:
            frameSkipSet = 60;
            frameskipCounter = 0;
            break;
        }
        break;
        
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASS wcex;
    
    memset(&wcex, 0, sizeof(WNDCLASS));

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszClassName  = szWindowClass;

    return RegisterClass(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    HWND hWnd;
    RECT winSize = {0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT};

    hInst = hInstance; // Store instance handle in our global variable
    
    AdjustWindowRectEx(&winSize, WS_OVERLAPPEDWINDOW, TRUE, WS_EX_OVERLAPPEDWINDOW);

    ghwnd = CreateWindowEx(WS_EX_OVERLAPPEDWINDOW, szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
    CW_USEDEFAULT, CW_USEDEFAULT, winSize.right - winSize.left, winSize.bottom - winSize.top, NULL, NULL, hInstance, NULL);

    if (!ghwnd)
    {
        return FALSE;
    }

    ShowWindow(ghwnd, nCmdShow);
    UpdateWindow(ghwnd);

    return TRUE;
}

uint16_t* lpBitmapBits;
HDC window_hdc;
HDC hdc_bmp;

void win32CreateBitmap()
{
    BITMAPINFO bmi = {};
    hdc_bmp = CreateCompatibleDC(NULL);

    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = DISPLAY_WIDTH;
    bmi.bmiHeader.biHeight = -DISPLAY_HEIGHT;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 16;
    bmi.bmiHeader.biCompression = BI_RGB;

    HBITMAP hbm = CreateDIBSection(hdc_bmp, &bmi, DIB_RGB_COLORS, (void**)&lpBitmapBits, NULL, NULL);
    HGDIOBJ oldbmp = SelectObject(hdc_bmp, hbm); 
}

//for fps counter, does not handle negative numbers
void intToStr(char* string, int num, int max)
{
    int numOfDigits = 0;
    int tempNum = num;
    //how many digits does the number have? (result is off by minus one)
    while (1)
    {
        tempNum = tempNum / 10;
        if (tempNum == 0)
            break;
        numOfDigits++;
    }
    //check if its above maximum digit limit
    if (numOfDigits+1 > max)
    {
        return;
    }
    //output the digits to string
    for (int i = 0; i <= numOfDigits; i++)
    {
        char digit = num % 10;
        string[numOfDigits-i] = '0'+digit;
        num = num / 10;
    }
}


unsigned int framesDrawn = 0;

int main(int argc, char **argv)
{
    LARGE_INTEGER largeint;
    MSG msg;
    HACCEL hAccelTable;
    HINSTANCE hInstance = GetModuleHandle(NULL);
    int nCmdShow = 1;
    DBGPRINTF("Game launch main()\n");
    ReadSaveFile(savePath);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        DBGPRINTF("Creating win32 window failed!\n");
        return FALSE;
    }
    DBGPRINTF("Window Init done!\n");
    window_hdc = GetDC(ghwnd);
    win32CreateBitmap();
    DBGPRINTF("Bitmap Init done!\n");
    
    //todo: convert these to int64
    QueryPerformanceCounter(&largeint);
    simTime = curGameTime = lastGameTime = largeint.QuadPart;

    isFrameAvailable = 0;
    vBlankSemaphore = CreateEvent(NULL, TRUE, FALSE, TEXT("vBlankEvent")); 
    if (vBlankSemaphore == NULL) 
    {
        DBGPRINTF("Could not create a event!\n");
        return 1;
    }
    
    DBGPRINTF("Event Init done!\n");

    cgb_audio_init(42048);
    DBGPRINTF("cgb_audio_init Init done!\n");
    
    VDraw();
    int ThreadID;
    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)DoMain, (LPVOID)&nCmdShow, 0, &ThreadID);
    DBGPRINTF("Thread Init done!\n");

    double accumulator = 0.0;

    memset(&internalClock, 0, sizeof(internalClock));
    internalClock.status = SIIRTCINFO_24HOUR;
    UpdateInternalClock();
    
    DBGPRINTF("Clock init done!\n");
    
    unsigned int fpsseconds = GetTickCount()+1000;
    while (isRunning)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (!paused)
        {
            double dt = fixedTimestep / timeScale; // TODO: Fix speedup

            QueryPerformanceCounter(&largeint); //todo error checking
            curGameTime = largeint.QuadPart;
            QueryPerformanceFrequency(&largeint);
            double deltaTime = (double)((curGameTime - lastGameTime) / largeint.QuadPart);
            if (deltaTime > (dt * 5))
                deltaTime = dt;
            lastGameTime = curGameTime;

            accumulator += deltaTime;

            while (accumulator >= dt)
            {
                if (isFrameAvailable)
                {
                    VDraw();
                    isFrameAvailable = 0;

                    REG_DISPSTAT |= INTR_FLAG_VBLANK;

                    RunDMAs(DMA_HBLANK);

                    if (REG_DISPSTAT & DISPSTAT_VBLANK_INTR)
                        gIntrTable[4]();
                    REG_DISPSTAT &= ~INTR_FLAG_VBLANK;

                    if(!SetEvent(vBlankSemaphore))
                    {
                        DBGPRINTF("Could not set vBlankSemaphore!");
                        return 1;
                    }
                    accumulator -= dt;
                }
                Sleep(0);
            }
           if (GetTickCount() > fpsseconds)
           {
                char titlebar[128] = {0};
                char fpscount[10] = {0};
                memcpy(titlebar, "win32 emerald fps:  ", sizeof("win32 emerald fps: "));
                intToStr(&titlebar[sizeof("win32 emerald fps: ")-1], framesDrawn, 10);
                SetWindowTextA(ghwnd, titlebar);
                framesDrawn = 0;
                fpsseconds = GetTickCount()+1000;
            }
        }

        if (videoScaleChanged)
        {
            videoScaleChanged = false;
        }

    }

    CloseSaveFile();
    CloseHandle(vBlankSemaphore);
    return 0;
}

static void ReadSaveFile(char *path)
{
    int bytesRead;
    // Check whether the saveFile exists, and create it if not
    sSaveFile = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (sSaveFile == INVALID_HANDLE_VALUE)
    {
        sSaveFile = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (sSaveFile == INVALID_HANDLE_VALUE) { DBGPRINTF("Invalid HANDLE at ReadSaveFile()\n"); }
    }

    int fileSize = SetFilePointer(sSaveFile, 0, 0, FILE_END);
    SetFilePointer(sSaveFile, 0, 0, FILE_BEGIN);

    // Only read as many bytes as fit inside the buffer
    // or as many bytes as are in the file
    int bytesToRead = (fileSize < sizeof(FLASH_BASE)) ? fileSize : sizeof(FLASH_BASE);

    ReadFile(sSaveFile, &FLASH_BASE, bytesToRead, &bytesRead, NULL);
    

    // Fill the buffer if the savefile was just created or smaller than the buffer itself
    for (int i = bytesRead; i < sizeof(FLASH_BASE); i++)
    {
        FLASH_BASE[i] = 0xFF;
    }
    CloseHandle(sSaveFile);
}

static void StoreSaveFile()
{
    DWORD bytesRead;
    sSaveFile = CreateFileA(savePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL); 
    if (sSaveFile != NULL || sSaveFile != INVALID_HANDLE_VALUE)
    {
        SetFilePointer(sSaveFile, 0, 0, FILE_BEGIN);
        WriteFile(sSaveFile, &FLASH_BASE, sizeof(FLASH_BASE), &bytesRead, NULL);
    }
    CloseHandle(sSaveFile);
}

void Platform_StoreSaveFile(void)
{
    StoreSaveFile();
}

void Platform_ReadFlash(u16 sectorNum, u32 offset, u8 *dest, u32 size)
{
    int bytesRead;
    DBGPRINTF("ReadFlash(sectorNum=0x%04X,offset=0x%08X,size=0x%02X)\n",sectorNum,offset,size);
    HANDLE savefile = CreateFileA(savePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL); 
    if (savefile == INVALID_HANDLE_VALUE)
    {
        DBGPRINTF("Error opening save file (GetLastError %u).\n", GetLastError());
        return;
    }
    if (SetFilePointer(savefile, (sectorNum << gFlash->sector.shift) + offset, 0, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
    {
        DBGPRINTF("SetFilePointer failed! (offset=%x) (GetLastError %u)\n", (sectorNum << gFlash->sector.shift), GetLastError());
        CloseHandle(savefile);
        return;
    }
    if (!ReadFile(savefile, dest, size, &bytesRead, NULL))
    {
        DBGPRINTF("ReadFile failed! (GetLastError %u)\n", GetLastError());
        CloseHandle(savefile);
        return;
    }
    CloseHandle(savefile);
}

void Platform_QueueAudio(float *audioBuffer, s32 samplesPerFrame)
{
    return;
}

bool32 Platform_SkipAudioFrame(void)
{
    return FALSE;
}

static void CloseSaveFile()
{
    if (sSaveFile != NULL || sSaveFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(sSaveFile);
    }
}

#ifdef xinputkeys
#define STICK_THRESHOLD 0.5f
u16 GetXInputKeys()
{
    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));

    DWORD dwResult = XInputGetState(0, &state);
    u16 xinputKeys = 0;

    if (dwResult == ERROR_SUCCESS)
    {
        /* A */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) >> 12;
        /* B */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_X) >> 13;
        /* Start */  xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_START) >> 1;
        /* Select */ xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) >> 3;
        /* L */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) << 1;
        /* R */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) >> 1;
        /* Up */     xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) << 6;
        /* Down */   xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) << 6;
        /* Left */   xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) << 3;
        /* Right */  xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) << 1;


        /* Control Stick */
        float xAxis = (float)state.Gamepad.sThumbLX / (float)SHRT_MAX;
        float yAxis = (float)state.Gamepad.sThumbLY / (float)SHRT_MAX;

        if (xAxis < -STICK_THRESHOLD) xinputKeys |= DPAD_LEFT;
        if (xAxis >  STICK_THRESHOLD) xinputKeys |= DPAD_RIGHT;
        if (yAxis < -STICK_THRESHOLD) xinputKeys |= DPAD_DOWN;
        if (yAxis >  STICK_THRESHOLD) xinputKeys |= DPAD_UP;


        /* Speedup */
        // Note: 'speedup' variable is only (un)set on keyboard input
        double oldTimeScale = timeScale;
        timeScale = (state.Gamepad.bRightTrigger > 0x80 || speedUp) ? 5.0 : 1.0;

        if (oldTimeScale != timeScale)
        {
            if (timeScale > 1.0)
            {
                //SDL_PauseAudio(1);
            }
            else
            {
                //SDL_ClearQueuedAudio(1);
                //SDL_PauseAudio(0);
            }
        }
    }

    return xinputKeys;
}
#endif // _WIN32

u16 Platform_GetKeyInput(void)
{
#ifdef xinputkeys
    u16 gamepadKeys = GetXInputKeys();
    return (gamepadKeys != 0) ? gamepadKeys : keys;
#endif

    return keys;
}

void VDraw()
{
    if (frameskipCounter == 0)
    {
        DrawFrame(lpBitmapBits);

        //convert pixels to to the correct format
        uint32_t* bitmap32 = (uint32_t*)lpBitmapBits; //cast to 32bit so we could convert two pixels at once
        while (bitmap32 != &lpBitmapBits[DISPLAY_HEIGHT * DISPLAY_WIDTH])
        {
            uint32_t color32 = *bitmap32;
            *bitmap32 = ((color32 & 0x1F001F) << 10) | (color32 & 0x83E083E0) | ((color32 & 0x7C007C00) >> 10);
            bitmap32++;
        }
        
        if (bitBltEnabled)
        {
            BitBlt(window_hdc, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, hdc_bmp, 0, 0, SRCCOPY);
            InvalidateRect(ghwnd, NULL, FALSE);
        }
        framesDrawn++;
    }
    frameskipCounter++;
    if (frameskipCounter == frameSkipSet)
        frameskipCounter = 0;
    
    REG_VCOUNT = 161; // prep for being in VBlank period
}

DWORD WINAPI DoMain(LPVOID lpParam)
{
    AgbMain();
    return 0;
}

void VBlankIntrWait(void)
{
    isFrameAvailable = 1;
    WaitForSingleObject(vBlankSemaphore, INFINITE);
    ResetEvent(vBlankSemaphore);
}

u8 BinToBcd(u8 bin)
{
    int placeCounter = 1;
    u8 out = 0;
    do
    {
        out |= (bin % 10) * placeCounter;
        placeCounter *= 16;
    }
    while ((bin /= 10) > 0);

    return out;
}

void Platform_GetStatus(struct SiiRtcInfo *rtc)
{
    rtc->status = internalClock.status;
}

void Platform_SetStatus(struct SiiRtcInfo *rtc)
{
    internalClock.status = rtc->status;
}

static void UpdateInternalClock(void)
{
    SYSTEMTIME time;
    GetLocalTime(&time);

    internalClock.year = BinToBcd(time.wYear - 100);
    internalClock.month = BinToBcd(time.wMonth-1) + 1;
    internalClock.day = BinToBcd(time.wDay);
    internalClock.dayOfWeek = BinToBcd(time.wDayOfWeek);
    internalClock.hour = BinToBcd(time.wHour);
    internalClock.minute = BinToBcd(time.wMinute);
    internalClock.second = BinToBcd(time.wSecond);
}

void Platform_GetDateTime(struct SiiRtcInfo *rtc)
{
    UpdateInternalClock();

    rtc->year = internalClock.year;
    rtc->month = internalClock.month;
    rtc->day = internalClock.day;
    rtc->dayOfWeek = internalClock.dayOfWeek;
    rtc->hour = internalClock.hour;
    rtc->minute = internalClock.minute;
    rtc->second = internalClock.second;
    DBGPRINTF("GetDateTime: %d-%02d-%02d %02d:%02d:%02d\n", ConvertBcdToBinary(rtc->year),
                                                         ConvertBcdToBinary(rtc->month),
                                                         ConvertBcdToBinary(rtc->day),
                                                         ConvertBcdToBinary(rtc->hour),
                                                         ConvertBcdToBinary(rtc->minute),
                                                         ConvertBcdToBinary(rtc->second));
}

void Platform_SetDateTime(struct SiiRtcInfo *rtc)
{
    internalClock.month = rtc->month;
    internalClock.day = rtc->day;
    internalClock.dayOfWeek = rtc->dayOfWeek;
    internalClock.hour = rtc->hour;
    internalClock.minute = rtc->minute;
    internalClock.second = rtc->second;
}

void Platform_GetTime(struct SiiRtcInfo *rtc)
{
    UpdateInternalClock();

    rtc->hour = internalClock.hour;
    rtc->minute = internalClock.minute;
    rtc->second = internalClock.second;
    DBGPRINTF("GetTime: %02d:%02d:%02d\n", ConvertBcdToBinary(rtc->hour),
                                        ConvertBcdToBinary(rtc->minute),
                                        ConvertBcdToBinary(rtc->second));
}

void Platform_SetTime(struct SiiRtcInfo *rtc)
{
    internalClock.hour = rtc->hour;
    internalClock.minute = rtc->minute;
    internalClock.second = rtc->second;
}

void Platform_SetAlarm(u8 *alarmData)
{
    // TODO
}

void SoftReset(u32 resetFlags)
{
    puts("Soft Reset called. Exiting.");
    ExitProcess(0);
}

#endif