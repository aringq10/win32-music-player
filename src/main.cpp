#ifndef UNICODE
#define UNICODE
#endif

// Headers and libraries
#include <windows.h>
#include <d2d1.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <thread>
#include <endpointvolume.h>
#include <commdlg.h>
#include <string>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Gdi32.lib")

// Globals
wchar_t g_Path[MAX_PATH] = L"";
HINSTANCE g_hInstance;
HWND g_hWnd = NULL;

ID2D1Factory*           g_pD2DFactory = nullptr;
ID2D1HwndRenderTarget*  g_pRenderTarget = nullptr;
ID2D1SolidColorBrush*   g_pBrush = nullptr;

D2D1_RECT_F g_rcButtonPrev;
D2D1_RECT_F g_rcButtonPlay;
D2D1_RECT_F g_rcButtonNext;
D2D1_RECT_F g_rcSliderProgress;
D2D1_RECT_F g_rcSliderVolume;
D2D1_RECT_F g_rcButtonOpenFile;

// Slider thumb positions
float g_progressValue = 0.0f; // 0.0 to 1.0
float g_volumeValue = 1.0f;   // 0.0 to 1.0
bool g_isDraggingProgress = false;
bool g_isDraggingVolume = false;

// Media Foundation globals
IMFMediaSession* g_pMediaSession = nullptr;
IMFMediaSource* g_pMediaSource = nullptr;

LONGLONG g_totalDuration = 0; // currently loaded song in 100ns units
bool g_isPlaying = false;
bool g_updateProgress = true;

// Forward declarations
class MediaSessionCallback;
MediaSessionCallback* g_pCallBack = nullptr;

HRESULT CreateGraphicsResources(HWND hwnd);
void DiscardGraphicsResources();
void OnPaint(HWND hwnd);
void Resize(HWND hwnd);
void CalculateLayout(float width, float height);
void UpdateProgressBar(HWND hwnd);

void OnLButtonDown(HWND hwnd, WPARAM wParam, LPARAM lParam);
void OnMouseMove(HWND hwnd, WPARAM wParam, LPARAM lParam);
void OnLButtonUp();
void OpenFileExplorer(HWND hwnd);

HRESULT InitMediaFoundation();
void CleanupMediaFoundation();
HRESULT InitializeMediaSession(const wchar_t* filePath);
HRESULT CreatePlaybackTopology(IMFMediaSource* pMediaSource, IMFMediaSession* pMediaSession, IMFTopology** ppTopology);
HRESULT AddSourceNode(IMFTopology* pTopology, IMFMediaSource* pMediaSource, IMFPresentationDescriptor* pPresentationDescriptor, IMFStreamDescriptor* pStreamDescriptor, IMFTopologyNode** ppNode);
HRESULT AddOutputNode(IMFTopology* pTopology, IMFActivate* pActivate, IMFTopologyNode** ppNode);
void PlayAudio();
void PauseAudio();
HRESULT GetCurrentPlaybackTime(LONGLONG* p_currentTime);
void SeekToTime(LONGLONG newTime100ns);
void SeekBySeconds(LONGLONG offsetSeconds);


LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// Helper functions
template <class T> void SafeRelease(T **ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

RECT ConvertRectFToRect(const D2D1_RECT_F& rectF)
{
    RECT rc;
    rc.left   = static_cast<LONG>(rectF.left);
    rc.top    = static_cast<LONG>(rectF.top);
    rc.right  = static_cast<LONG>(rectF.right);
    rc.bottom = static_cast<LONG>(rectF.bottom);
    return rc;
}

void SetMusicVolume(float volumeLevel)
{
    if (g_pMediaSession)
    {
        IMFSimpleAudioVolume* pAudioVolume = nullptr;

        // Get the IMFSimpleAudioVolume interface from the Media Session
        HRESULT hr = MFGetService(
            g_pMediaSession,           // Pointer to the Media Session
            MR_POLICY_VOLUME_SERVICE,  // Service identifier
            IID_PPV_ARGS(&pAudioVolume) // Interface ID and pointer
        );

        if (SUCCEEDED(hr))
        {
            // Clamp the volume level between 0.0 and 1.0
            if (volumeLevel < 0.0f) volumeLevel = 0.0f;
            if (volumeLevel > 1.0f) volumeLevel = 1.0f;

            // Set the volume level
            hr = pAudioVolume->SetMasterVolume(volumeLevel);
            if (FAILED(hr))
            {
                MessageBox(NULL, L"Failed to set volume.", L"Error", MB_ICONERROR);
            }
        }
        else
        {
            MessageBox(NULL, L"Failed to get audio volume service.", L"Error", MB_ICONERROR);
        }

        // Release the audio volume interface
        SafeRelease(&pAudioVolume);
    }
}

class MediaSessionCallback : public IMFAsyncCallback
{
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        if (ppv == nullptr) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IMFAsyncCallback)
        {
            *ppv = static_cast<IMFAsyncCallback*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_refCount); }
    STDMETHODIMP_(ULONG) Release()
    {
        ULONG count = InterlockedDecrement(&m_refCount);
        if (count == 0) delete this;
        return count;
    }

    STDMETHODIMP GetParameters(DWORD*, DWORD*) { return E_NOTIMPL; }

    STDMETHODIMP Invoke(IMFAsyncResult* pResult)
    {
        IMFMediaEvent* pEvent = nullptr;
        MediaEventType eventType = MEUnknown;
    
        if (g_pMediaSession)
        {
            HRESULT hr = g_pMediaSession->EndGetEvent(pResult, &pEvent);
            if (SUCCEEDED(hr))
            {
                pEvent->GetType(&eventType);
    
                switch (eventType)
                {
                    case MESessionEnded:
                        // Reset the UI state
                        g_progressValue = 0.0f;
                        g_isPlaying = false;
                        wcscpy_s(g_Path, MAX_PATH, L"");
                        InvalidateRect(NULL, NULL, FALSE);

                        MessageBox(NULL, L"Song ended", L"Info", MB_OK);
                        break;
                    
                    case MEError:
                        MessageBox(NULL, L"An error occurred during playback.", L"Error", MB_ICONERROR);
                        break;
                }
    
                // Continue listening for events
                g_pMediaSession->BeginGetEvent(this, NULL);
            }
            SafeRelease(&pEvent);
        }
        return S_OK;
    }

private:
    LONG m_refCount = 1;
};

// WinMain
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
    {
        MessageBox(NULL, L"COM Initialization failed", L"Error", MB_ICONERROR);
        return -1;
    }

    g_hInstance = hInstance;
    LPCWSTR wndClass = L"AudioPlayerWindowClass";

    // Register window class
    WNDCLASS wc = { };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = wndClass;

    RegisterClass(&wc);

    // Create window
    g_hWnd = CreateWindowEx(
        0,
        wndClass,
        L"Audio Player",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 
        CW_USEDEFAULT, 
        800, 
        600,
        NULL, 
        NULL, 
        hInstance, 
        NULL
    );

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return 0;
}

// Graphics functions
HRESULT CreateGraphicsResources(HWND hwnd)
{
    HRESULT hr = S_OK;
    if (g_pRenderTarget == NULL)
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
    
        D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
    
        hr = g_pD2DFactory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(hwnd, size),
            &g_pRenderTarget
        );
        if (FAILED(hr)) return hr;
    
        hr = g_pRenderTarget->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::Black),
            &g_pBrush
        );
        if (FAILED(hr)) return hr;
    }
    return hr;
}

void DiscardGraphicsResources()
{
    SafeRelease(&g_pBrush);
    SafeRelease(&g_pRenderTarget);
}

void OnPaint(HWND hwnd)
{
    HRESULT hr = CreateGraphicsResources(hwnd);
    if (SUCCEEDED(hr))
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);

        g_pRenderTarget->BeginDraw();
        g_pRenderTarget->Clear(D2D1::ColorF(0.13, 0.13, 0.13, 1.0));

        // === Draw Buttons ===
        g_pBrush->SetColor(D2D1::ColorF(D2D1::ColorF::Gray));
        g_pRenderTarget->FillRectangle(g_rcButtonPrev, g_pBrush);
        g_pRenderTarget->FillRectangle(g_rcButtonNext, g_pBrush);
        g_pRenderTarget->FillRectangle(g_rcButtonOpenFile, g_pBrush);
        if (g_isPlaying)
        {
            g_pBrush->SetColor(D2D1::ColorF(D2D1::ColorF::Red));
            g_pRenderTarget->FillRectangle(g_rcButtonPlay, g_pBrush);
        } else
        {
            g_pBrush->SetColor(D2D1::ColorF(D2D1::ColorF::Green));
            g_pRenderTarget->FillRectangle(g_rcButtonPlay, g_pBrush);
        }


        // === Draw Progress Slider ===
        g_pBrush->SetColor(D2D1::ColorF(D2D1::ColorF::LightGray));
        g_pRenderTarget->FillRectangle(g_rcSliderProgress, g_pBrush);

        // Progress Thumb
        float progressX = g_rcSliderProgress.left + g_progressValue * (g_rcSliderProgress.right - g_rcSliderProgress.left);
        D2D1_RECT_F thumbProgress = D2D1::RectF(progressX - 5, g_rcSliderProgress.top - 5, progressX + 5, g_rcSliderProgress.bottom + 5);
        g_pBrush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
        g_pRenderTarget->FillRectangle(thumbProgress, g_pBrush);

        // === Draw Volume Slider ===
        g_pBrush->SetColor(D2D1::ColorF(D2D1::ColorF::LightGray));
        g_pRenderTarget->FillRectangle(g_rcSliderVolume, g_pBrush);

        // Volume Thumb
        float volumeX = g_rcSliderVolume.left + g_volumeValue * (g_rcSliderVolume.right - g_rcSliderVolume.left);
        D2D1_RECT_F thumbVolume = D2D1::RectF(volumeX - 5, g_rcSliderVolume.top - 5, volumeX + 5, g_rcSliderVolume.bottom + 5);
        g_pBrush->SetColor(D2D1::ColorF(D2D1::ColorF::Green));
        g_pRenderTarget->FillRectangle(thumbVolume, g_pBrush);

        HRESULT hr = g_pRenderTarget->EndDraw();
        if (FAILED(hr) || hr == D2DERR_RECREATE_TARGET) {
            DiscardGraphicsResources();
        }

        EndPaint(hwnd, &ps);
    }
}

void Resize(HWND hwnd)
{
    if (g_pRenderTarget)
    {
        RECT rc;
        GetClientRect(hwnd, &rc);

        D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
        g_pRenderTarget->Resize(size);

        // Dynamically reposition buttons
        float width = (float)size.width;
        float height = (float)size.height;

        CalculateLayout(width, height);

        InvalidateRect(hwnd, NULL, FALSE);
    }
}

void CalculateLayout(float width, float height)
{
    g_rcButtonPrev = D2D1::RectF(width * 0.05f, height * 0.85f, width * 0.15f, height * 0.95f);
    g_rcButtonPlay = D2D1::RectF(width * 0.20f, height * 0.83f, width * 0.35f, height * 0.97f);
    g_rcButtonNext = D2D1::RectF(width * 0.40f, height * 0.85f, width * 0.50f, height * 0.95f);

    g_rcSliderProgress = D2D1::RectF(width * 0.05f, height * 0.75f, width * 0.95f, height * 0.78f);
    g_rcSliderVolume   = D2D1::RectF(width * 0.70f, height * 0.85f, width * 0.95f, height * 0.90f);

    g_rcButtonOpenFile = D2D1::RectF(width * 0.55f, height * 0.85f, width * 0.65f, height * 0.95f);
}

void UpdateProgressBar(HWND hwnd)
{
    if (!g_pMediaSession || !g_pMediaSource) return;

    // Get the current playback time
    LONGLONG currentTime = 0;
    HRESULT hr = GetCurrentPlaybackTime(&currentTime);
    if (FAILED(hr)) return;

    // Update the progress value (0.0 to 1.0)
    g_progressValue = (double)currentTime / (double)g_totalDuration;
    if (g_progressValue < 0.0) g_progressValue = 0.0;
    if (g_progressValue > 1.0) g_progressValue = 1.0;

    // Redraw the window to update the progress bar
    InvalidateRect(hwnd, NULL, FALSE);
}

// Mouse/Input events
void OnLButtonDown(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    SetCapture(hwnd); // Capture mouse input even if outside window
    POINT pt = { (SHORT)LOWORD(lParam), (SHORT)HIWORD(lParam) };
    D2D1_POINT_2F movePoint = D2D1::Point2F((FLOAT)pt.x, (FLOAT)pt.y);

    // Check buttons
    if (PtInRect(&ConvertRectFToRect(g_rcButtonPlay), pt)) {
        // Check if a file has been selected
        if (wcslen(g_Path) == 0) {
            MessageBox(hwnd, L"No file selected. Please select a file before playing.", L"Warning", MB_ICONWARNING);
            return;
        }
        
        if (!g_isPlaying)
        {
            g_isPlaying = true;
            PlayAudio();
        }
        else
        {
            g_isPlaying = false;
            PauseAudio();
        }
        InvalidateRect(hwnd, NULL, FALSE);
    }
    else if (PtInRect(&ConvertRectFToRect(g_rcButtonPrev), pt)) {
        MessageBox(hwnd, L"Previous button clicked!", L"Info", MB_OK);
    }
    else if (PtInRect(&ConvertRectFToRect(g_rcButtonNext), pt)) {
        MessageBox(hwnd, L"Next button clicked!", L"Info", MB_OK);
    }
    // Check if clicked on progress bar
    else if (PtInRect(&ConvertRectFToRect(g_rcSliderProgress), pt)) {
        if (!g_isPlaying) return;
        g_updateProgress = false;
        g_isDraggingProgress = true;

        float sliderWidth = g_rcSliderProgress.right - g_rcSliderProgress.left;

        // Clamp movePoint.x to the slider bounds
        if (movePoint.x < g_rcSliderProgress.left) movePoint.x = g_rcSliderProgress.left;
        if (movePoint.x > g_rcSliderProgress.right) movePoint.x = g_rcSliderProgress.right;

        g_progressValue = (movePoint.x - g_rcSliderProgress.left) / sliderWidth;

        InvalidateRect(hwnd, NULL, FALSE);
    }
    // Check if clicked on volume bar
    else if (PtInRect(&ConvertRectFToRect(g_rcSliderVolume), pt)) {
        g_isDraggingVolume = true;

        float sliderWidth = g_rcSliderVolume.right - g_rcSliderVolume.left;

        // Clamp movePoint.x to the slider bounds
        if (movePoint.x < g_rcSliderVolume.left) movePoint.x = g_rcSliderVolume.left;
        if (movePoint.x > g_rcSliderVolume.right) movePoint.x = g_rcSliderVolume.right;

        g_volumeValue = (movePoint.x - g_rcSliderVolume.left) / sliderWidth;

        SetMusicVolume(g_volumeValue);

        InvalidateRect(hwnd, NULL, FALSE);
    }
    else if (PtInRect(&ConvertRectFToRect(g_rcButtonOpenFile), pt)) {
        OpenFileExplorer(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

void OnMouseMove(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    if (wParam & MK_LBUTTON) {
        POINT pt = { (SHORT)LOWORD(lParam), (SHORT)HIWORD(lParam) };
        D2D1_POINT_2F movePoint = D2D1::Point2F((FLOAT)pt.x, (FLOAT)pt.y);

        if (g_isDraggingProgress) {
            float sliderWidth = g_rcSliderProgress.right - g_rcSliderProgress.left;

            // Clamp movePoint.x to the slider bounds
            if (movePoint.x < g_rcSliderProgress.left) movePoint.x = g_rcSliderProgress.left;
            if (movePoint.x > g_rcSliderProgress.right) movePoint.x = g_rcSliderProgress.right;

            g_progressValue = (movePoint.x - g_rcSliderProgress.left) / sliderWidth;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        else if (g_isDraggingVolume) {
            float sliderWidth = g_rcSliderVolume.right - g_rcSliderVolume.left;

            // Clamp movePoint.x to the slider bounds
            if (movePoint.x < g_rcSliderVolume.left) movePoint.x = g_rcSliderVolume.left;
            if (movePoint.x > g_rcSliderVolume.right) movePoint.x = g_rcSliderVolume.right;

            g_volumeValue = (movePoint.x - g_rcSliderVolume.left) / sliderWidth;

            SetMusicVolume(g_volumeValue);

            InvalidateRect(hwnd, NULL, FALSE);
        }
    }
}

void OnLButtonUp()
{
    ReleaseCapture();
    if (g_isDraggingProgress)
    {
        SeekToTime((LONGLONG)(g_progressValue * g_totalDuration));
    }
    g_updateProgress = true;
    g_isDraggingProgress = false;
    g_isDraggingVolume = false;
}

void OpenFileExplorer(HWND hwnd)
{
    
    OPENFILENAME ofn;       // Common dialog box structure
    wchar_t szFile[MAX_PATH] = L""; // Buffer for file name

    // Initialize OPENFILENAME
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(szFile[0]);
    ofn.lpstrFilter = L"MP3 Files\0*.MP3\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    // Display the Open dialog box
    if (GetOpenFileName(&ofn) == TRUE) {
        // Assign the selected file path to g_Path
        wcscpy_s(g_Path, MAX_PATH, ofn.lpstrFile);

        // Initialize the Media Session with the selected file
        HRESULT hr = InitializeMediaSession(g_Path);
        if (FAILED(hr)) {
            MessageBox(hwnd, L"Failed to initialize Media Session for the selected file.", L"Error", MB_ICONERROR);
        } else {
            MessageBox(hwnd, L"File loaded successfully! Ready to play.", L"Info", MB_OK);
        }
    }
}

// Audio Handling
HRESULT InitMediaFoundation()
{
    return MFStartup(MF_VERSION);
}

void CleanupMediaFoundation()
{
    SafeRelease(&g_pMediaSession);
    SafeRelease(&g_pMediaSource);
    SafeRelease(&g_pCallBack);
    MFShutdown();
}

HRESULT InitializeMediaSession(const wchar_t* filePath)
{
    HRESULT hr = S_OK;
    IMFSourceResolver* pSourceResolver = nullptr;
    IUnknown* pSource = nullptr;
    IMFTopology* pTopology = nullptr;

    // Create the MediaSession
    hr = MFCreateMediaSession(NULL, &g_pMediaSession);
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create MediaSession", L"Error", MB_ICONERROR);
        goto done;
    }

    // Register for Media Session events
    g_pCallBack = new MediaSessionCallback();
    hr = g_pMediaSession->BeginGetEvent(g_pCallBack, NULL);
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to register for Media Session events", L"Error", MB_ICONERROR);
        goto done;
    }

    // Create the Source Resolver
    hr = MFCreateSourceResolver(&pSourceResolver);
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create Source Resolver", L"Error", MB_ICONERROR);
        goto done;
    }

    // Use the Source Resolver to create a Media Source
    MF_OBJECT_TYPE objectType;
    hr = pSourceResolver->CreateObjectFromURL(
        filePath,                      // URL of the audio file
        MF_RESOLUTION_MEDIASOURCE,     // Create a Media Source
        NULL,                          // Optional property store
        &objectType,                   // Receives the object type
        &pSource                       // Receives the Media Source
    );
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create Media Source", L"Error", MB_ICONERROR);
        goto done;
    }

    // Get the IMFMediaSource interface
    hr = pSource->QueryInterface(IID_PPV_ARGS(&g_pMediaSource));
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to query Media Source", L"Error", MB_ICONERROR);
        goto done;
    }

    // Create the topology
    hr = CreatePlaybackTopology(g_pMediaSource, g_pMediaSession, &pTopology);
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create playback topology", L"Error", MB_ICONERROR);
        goto done;
    }

    // Set the topology on the MediaSession
    hr = g_pMediaSession->SetTopology(0, pTopology);
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to set topology on MediaSession", L"Error", MB_ICONERROR);
        goto done;
    }

done:
    SafeRelease(&pSourceResolver);
    SafeRelease(&pSource);
    SafeRelease(&pTopology);
    return hr;
}

HRESULT CreatePlaybackTopology(IMFMediaSource* pMediaSource, IMFMediaSession* pMediaSession, IMFTopology** ppTopology)
{
    HRESULT hr = S_OK;
    IMFTopology* pTopology = nullptr;
    IMFPresentationDescriptor* pPresentationDescriptor = nullptr;
    IMFStreamDescriptor* pStreamDescriptor = nullptr;
    IMFActivate* pAudioRendererActivate = nullptr;
    IMFTopologyNode* pSourceNode = nullptr;
    IMFTopologyNode* pOutputNode = nullptr;
    BOOL streamSelected = FALSE;

    // Create a new topology
    hr = MFCreateTopology(&pTopology);
    if (FAILED(hr)) goto done;

    // Get the presentation descriptor for the media source
    hr = pMediaSource->CreatePresentationDescriptor(&pPresentationDescriptor);
    if (FAILED(hr)) goto done;
    else 
    {   // Get the total duration of the loaded media
        pPresentationDescriptor->GetUINT64(MF_PD_DURATION, (UINT64*)&g_totalDuration);
    }

    // Get the first audio stream
    hr = pPresentationDescriptor->GetStreamDescriptorByIndex(0, &streamSelected, &pStreamDescriptor);
    if (FAILED(hr)) goto done;

    if (streamSelected)
    {   
        // Create the audio renderer activation object
        hr = MFCreateAudioRendererActivate(&pAudioRendererActivate);
        if (FAILED(hr)) goto done;

        // Add a source node for the media source
        hr = AddSourceNode(pTopology, pMediaSource, pPresentationDescriptor, pStreamDescriptor, &pSourceNode);
        if (FAILED(hr)) goto done;

        // Add an output node for the audio renderer
        hr = AddOutputNode(pTopology, pAudioRendererActivate, &pOutputNode);
        if (FAILED(hr)) goto done;

        hr = pSourceNode->ConnectOutput(0, pOutputNode, 0);
        if (FAILED(hr)) goto done;
    }
    else
    {
        MessageBox(NULL, L"No valid audio stream found in the file.", L"Error", MB_ICONERROR);
        goto done; 
    }

    *ppTopology = pTopology;
    (*ppTopology)->AddRef();

done:
    SafeRelease(&pTopology);
    SafeRelease(&pPresentationDescriptor);
    SafeRelease(&pStreamDescriptor);
    SafeRelease(&pAudioRendererActivate);
    return hr;
}

HRESULT AddSourceNode(IMFTopology* pTopology, IMFMediaSource* pMediaSource, IMFPresentationDescriptor* pPresentationDescriptor, IMFStreamDescriptor* pStreamDescriptor, IMFTopologyNode** ppNode)
{
    HRESULT hr = S_OK;
    IMFTopologyNode* pNode = nullptr;

    // Create the source node
    hr = MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE, &pNode);
    if (FAILED(hr)) goto done;

    // Set the attributes on the source node
    hr = pNode->SetUnknown(MF_TOPONODE_SOURCE, pMediaSource);
    if (FAILED(hr)) goto done;

    hr = pNode->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR, pPresentationDescriptor);
    if (FAILED(hr)) goto done;

    hr = pNode->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, pStreamDescriptor);
    if (FAILED(hr)) goto done;

    // Add the node to the topology
    hr = pTopology->AddNode(pNode);
    if (FAILED(hr)) goto done;

    // Return the created node
    *ppNode = pNode;
    (*ppNode)->AddRef();

done:
    SafeRelease(&pNode);
    return hr;
}

HRESULT AddOutputNode(IMFTopology* pTopology, IMFActivate* pActivate, IMFTopologyNode** ppNode)
{
    HRESULT hr = S_OK;
    IMFTopologyNode* pNode = nullptr;

    // Create the output node
    hr = MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &pNode);
    if (FAILED(hr)) goto done;

    // Set the activation object on the output node
    hr = pNode->SetObject(pActivate);
    if (FAILED(hr)) goto done;

    // Add the node to the topology
    hr = pTopology->AddNode(pNode);
    if (FAILED(hr)) goto done;

    // Return the created node
    *ppNode = pNode;
    (*ppNode)->AddRef();

done:
    SafeRelease(&pNode);
    return hr;
}

void PlayAudio()
{
    if (g_pMediaSession)
    {
        PROPVARIANT varStart;
        PropVariantInit(&varStart);
        HRESULT hr = g_pMediaSession->Start(&GUID_NULL, &varStart);
        PropVariantClear(&varStart);

        if (FAILED(hr)) {
            MessageBox(NULL, L"play pressed failed", L"Error", MB_ICONERROR);
        }
    }
}

void PauseAudio()
{
    if (g_pMediaSession)
    {
        g_pMediaSession->Pause();
    }
}

HRESULT GetCurrentPlaybackTime(LONGLONG* p_currentTime)
{
    if (!g_pMediaSession)
        return 0;

    IMFClock* pClock = nullptr;
    LONGLONG currentTime = 0, systemTime = 0;

    HRESULT hr = g_pMediaSession->GetClock(&pClock);
    if(FAILED(hr)) goto done;

    hr = pClock->GetCorrelatedTime(0, &currentTime, &systemTime);
    if(FAILED(hr)) goto done;

    *p_currentTime = currentTime;

done:
    SafeRelease(&pClock);
    return hr;
}

void SeekToTime(LONGLONG newTime100ns)
{
    if (!g_pMediaSession || !g_pMediaSource) return;

    IMFPresentationDescriptor* pPD = nullptr;
    UINT64 totalDuration = 0;


    if (newTime100ns < 0) newTime100ns = 0;
    if (newTime100ns > g_totalDuration)
        newTime100ns = g_totalDuration;

    PROPVARIANT varStart;
    PropVariantInit(&varStart);
    varStart.vt = VT_I8;
    varStart.hVal.QuadPart = newTime100ns;

    HRESULT hr = g_pMediaSession->Start(&GUID_NULL, &varStart);
    if (FAILED(hr)) {
        MessageBox(NULL, L"Seek failed", L"Error", MB_ICONERROR);
    }

    PropVariantClear(&varStart);
    SafeRelease(&pPD);
}

void SeekBySeconds(LONGLONG offsetSeconds)
{
    if (!g_pMediaSession || !g_pMediaSource) return;

    LONGLONG currentTime = 0;
    HRESULT hr = GetCurrentPlaybackTime(&currentTime);
    if (FAILED(hr)) return;
    
    SeekToTime(currentTime + offsetSeconds * 10000000);
}

// Window Procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) 
{
    switch (msg)
    {
    case WM_CREATE:
        // Initialize D2D1
        if (FAILED(D2D1CreateFactory(
                D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_pD2DFactory)))
        {
            MessageBox(hwnd, L"Direct2D Factory initialization failed", L"Error", MB_ICONERROR);
            return -1;  // Fail CreateWindowEx.
        }
        if (FAILED(CreateGraphicsResources(hwnd)))
        {
            MessageBox(hwnd, L"Direct2D initialization failed", L"Error", MB_ICONERROR);
            return -1;
        }

        // Initialize Media Foundation
        if (FAILED(InitMediaFoundation()))
        {
            MessageBox(hwnd, L"Media Foundation initialization failed", L"Error", MB_ICONERROR);
            return -1;
        }

        // Set a timer to update the progress bar every 100ms
        SetTimer(hwnd, 1, 100, NULL);

        // Setup initial button positions
        Resize(hwnd);
        break;
    
    case WM_PAINT:
        OnPaint(hwnd);
        break;

    case WM_SIZE:
        Resize(hwnd);
        break;

    case WM_LBUTTONDOWN:
        OnLButtonDown(hwnd, wParam, lParam);
        break;
    
    case WM_MOUSEMOVE:
        OnMouseMove(hwnd, wParam, lParam);
        break;

    case WM_LBUTTONUP:
        OnLButtonUp();
        break;
    
    case WM_KEYDOWN:
        switch (wParam)
        {
        case VK_SPACE: // Spacebar to play/pause
            if (wcslen(g_Path) == 0) 
            {
                MessageBox(hwnd, L"No file selected. Please select a file before playing.", L"Warning", MB_ICONWARNING);
            }
            else
            {
                if (!g_isPlaying)
                {
                    g_isPlaying = true;
                    PlayAudio();
                }
                else
                {
                    g_isPlaying = false;
                    PauseAudio();
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
            break;
    
        case VK_LEFT: // Left arrow to go to the previous track
            SeekBySeconds(-5);
            break;
    
        case VK_RIGHT: // Right arrow to go to the next track
            SeekBySeconds(5);
            break;
    
        case VK_UP: // Up arrow to increase volume
            g_volumeValue += 0.1f;
            if (g_volumeValue > 1.0f) g_volumeValue = 1.0f;
            SetMusicVolume(g_volumeValue);
            InvalidateRect(hwnd, NULL, FALSE);
            break;
    
        case VK_DOWN: // Down arrow to decrease volume
            g_volumeValue -= 0.1f;
            if (g_volumeValue < 0.0f) g_volumeValue = 0.0f;
            SetMusicVolume(g_volumeValue);
            InvalidateRect(hwnd, NULL, FALSE);
            break;

        case 'O': // 'O' key to open the file dialog
            OpenFileExplorer(hwnd);
            break;
    
        default:
            break;
        }
        break;

    case WM_TIMER:
        if (wParam == 1 && g_isPlaying && g_updateProgress) 
        {
            UpdateProgressBar(hwnd);
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        CleanupMediaFoundation();
        DiscardGraphicsResources();
        SafeRelease(&g_pD2DFactory);
        PostQuitMessage(0);
        break;
    
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}