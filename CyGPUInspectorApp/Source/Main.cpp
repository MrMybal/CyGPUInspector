// CyGPUInspectorApp — Win32 + D3D11 + Dear ImGui host.
//
// The application renders with D3D11 for one reason beyond the UI itself: it has to open the
// textures the add-on shares from the game process and display them without a CPU round trip
// (milestone 3). Everything else lives in Application.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "App/Application.hpp"
#include "Resource.h"

#include <CyGPUInspectorCore/Version.hpp>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wincodec.h>
#include <tchar.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <shellapi.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
	ID3D11Device *g_device = nullptr;
	ID3D11DeviceContext *g_context = nullptr;
	IDXGISwapChain *g_swapchain = nullptr;
	ID3D11RenderTargetView *g_render_target = nullptr;
	bool g_swapchain_occluded = false;
	UINT g_resize_width = 0;
	UINT g_resize_height = 0;

	void CreateRenderTarget()
	{
		ID3D11Texture2D *back_buffer = nullptr;
		if (SUCCEEDED(g_swapchain->GetBuffer(0, IID_PPV_ARGS(&back_buffer))) && back_buffer != nullptr)
		{
			g_device->CreateRenderTargetView(back_buffer, nullptr, &g_render_target);
			back_buffer->Release();
		}
	}

	void CleanupRenderTarget()
	{
		if (g_render_target != nullptr)
		{
			g_render_target->Release();
			g_render_target = nullptr;
		}
	}

	// The adapter the interface renders with, for the preview to compare with the game's.
	std::string g_adapter_name;

	// A shared texture can only be opened on the adapter that created it, and games run on the
	// high performance one. Letting Windows pick "the default" put this application on the
	// integrated GPU of a machine that has two, where no preview of the game could ever open.
	IDXGIAdapter *HighPerformanceAdapter()
	{
		IDXGIFactory6 *factory = nullptr;
		if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
			return nullptr;
		IDXGIAdapter *adapter = nullptr;
		if (FAILED(factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter))))
			adapter = nullptr;
		factory->Release();
		return adapter;
	}

	std::string AdapterNameOf(ID3D11Device *device)
	{
		std::string name;
		IDXGIDevice *dxgi_device = nullptr;
		if (device == nullptr || FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgi_device))))
			return name;
		IDXGIAdapter *adapter = nullptr;
		if (SUCCEEDED(dxgi_device->GetAdapter(&adapter)))
		{
			DXGI_ADAPTER_DESC desc = {};
			if (SUCCEEDED(adapter->GetDesc(&desc)))
			{
				const int length = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nullptr, 0, nullptr, nullptr);
				if (length > 1)
				{
					name.resize(static_cast<size_t>(length) - 1);
					WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name.data(), length, nullptr, nullptr);
				}
			}
			adapter->Release();
		}
		dxgi_device->Release();
		return name;
	}

	// The logo drawn in the interface, decoded from the PNG embedded in the executable, so it is
	// there whatever folder the executable was copied to. nullptr if anything fails: the interface
	// only loses a picture.
	ID3D11ShaderResourceView *LoadLogo(HINSTANCE instance, ID3D11Device *device)
	{
		const HRSRC found = FindResourceW(instance, MAKEINTRESOURCEW(IDR_LOGO_PNG), MAKEINTRESOURCEW(10));
		const HGLOBAL loaded = found != nullptr ? LoadResource(instance, found) : nullptr;
		const void *bytes = loaded != nullptr ? LockResource(loaded) : nullptr;
		const DWORD size = found != nullptr ? SizeofResource(instance, found) : 0;
		if (bytes == nullptr || size == 0 || device == nullptr)
			return nullptr;

		const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		ID3D11ShaderResourceView *view = nullptr;
		IWICImagingFactory *factory = nullptr;
		IWICStream *stream = nullptr;
		IWICBitmapDecoder *decoder = nullptr;
		IWICBitmapFrameDecode *frame = nullptr;
		IWICFormatConverter *converter = nullptr;
		do
		{
			if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
			    FAILED(factory->CreateStream(&stream)) ||
			    FAILED(stream->InitializeFromMemory(static_cast<BYTE *>(const_cast<void *>(bytes)), size)) ||
			    FAILED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
			    FAILED(decoder->GetFrame(0, &frame)) ||
			    FAILED(factory->CreateFormatConverter(&converter)) ||
			    FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr,
			                                 0.0, WICBitmapPaletteTypeCustom)))
				break;

			UINT width = 0;
			UINT height = 0;
			converter->GetSize(&width, &height);
			std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 4);
			if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data())))
				break;

			D3D11_TEXTURE2D_DESC desc = {};
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_IMMUTABLE;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			const D3D11_SUBRESOURCE_DATA data = { pixels.data(), width * 4, 0 };
			ID3D11Texture2D *texture = nullptr;
			if (SUCCEEDED(device->CreateTexture2D(&desc, &data, &texture)))
			{
				device->CreateShaderResourceView(texture, nullptr, &view);
				texture->Release();
			}
		} while (false);

		if (converter != nullptr) converter->Release();
		if (frame != nullptr) frame->Release();
		if (decoder != nullptr) decoder->Release();
		if (stream != nullptr) stream->Release();
		if (factory != nullptr) factory->Release();
		if (SUCCEEDED(com))
			CoUninitialize();
		return view;
	}

	bool CreateDeviceD3D(HWND window)
	{
		DXGI_SWAP_CHAIN_DESC desc = {};
		desc.BufferCount = 2;
		desc.BufferDesc.Width = 0;
		desc.BufferDesc.Height = 0;
		desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.BufferDesc.RefreshRate.Numerator = 60;
		desc.BufferDesc.RefreshRate.Denominator = 1;
		desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.OutputWindow = window;
		desc.SampleDesc.Count = 1;
		desc.Windowed = TRUE;
		desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

		const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
		D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;

		HRESULT hr = E_FAIL;
		if (IDXGIAdapter *adapter = HighPerformanceAdapter())
		{
			hr = D3D11CreateDeviceAndSwapChain(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, levels,
				static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &desc, &g_swapchain, &g_device, &obtained,
				&g_context);
			adapter->Release();
		}
		if (FAILED(hr))
			hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels,
				static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &desc, &g_swapchain, &g_device, &obtained,
				&g_context);
		if (hr == DXGI_ERROR_UNSUPPORTED)
		{
			hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels,
				static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &desc, &g_swapchain, &g_device, &obtained,
				&g_context);
		}
		if (FAILED(hr))
			return false;

		g_adapter_name = AdapterNameOf(g_device);
		CreateRenderTarget();
		return true;
	}

	void CleanupDeviceD3D()
	{
		CleanupRenderTarget();
		if (g_swapchain != nullptr) { g_swapchain->Release(); g_swapchain = nullptr; }
		if (g_context != nullptr) { g_context->Release(); g_context = nullptr; }
		if (g_device != nullptr) { g_device->Release(); g_device = nullptr; }
	}
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
	LRESULT WINAPI WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
	{
		if (ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam))
			return true;

		switch (message)
		{
		case WM_SIZE:
			if (wparam == SIZE_MINIMIZED)
				return 0;
			g_resize_width = static_cast<UINT>(LOWORD(lparam));
			g_resize_height = static_cast<UINT>(HIWORD(lparam));
			return 0;
		case WM_SYSCOMMAND:
			if ((wparam & 0xfff0) == SC_KEYMENU) // disable the ALT application menu
				return 0;
			break;
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		default:
			break;
		}
		return DefWindowProcW(window, message, wparam, lparam);
	}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
	// The logo in the title bar and the taskbar, at the sizes each of them asks for.
	const HICON icon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_CYGPUINSPECTOR), IMAGE_ICON,
		GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
	const HICON small_icon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_CYGPUINSPECTOR), IMAGE_ICON,
		GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
	WNDCLASSEXW window_class = { sizeof(window_class), CS_CLASSDC, WndProc, 0L, 0L, instance, icon, nullptr,
		nullptr, nullptr, L"CyGPUInspectorApp", small_icon };
	RegisterClassExW(&window_class);

	const HWND window = CreateWindowW(window_class.lpszClassName,
		L"CyGPUInspector " CYGI_VERSION_STRING L" — GPU frame, shader and resource inspector",
		WS_OVERLAPPEDWINDOW, 80, 60, 1760, 1000, nullptr, nullptr, instance, nullptr);

	if (!CreateDeviceD3D(window))
	{
		CleanupDeviceD3D();
		UnregisterClassW(window_class.lpszClassName, instance);
		MessageBoxW(nullptr, L"Direct3D 11 could not be initialized.", L"CyGPUInspector", MB_ICONERROR);
		return 1;
	}

	ShowWindow(window, SW_SHOWDEFAULT);
	UpdateWindow(window);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // multi monitor: panels can leave the window
	// Beside the executable rather than in the working directory, so the layout follows the
	// application however it was started. ImGui keeps the pointer, so the string has to outlive
	// this scope.
	static std::string ini_path = [] {
		wchar_t executable[MAX_PATH] = {};
		if (GetModuleFileNameW(nullptr, executable, MAX_PATH) == 0)
			return std::string("CyGPUInspectorApp.ini");
		return (std::filesystem::path(executable).parent_path() / "CyGPUInspectorApp.ini").string();
	}();
	io.IniFilename = ini_path.c_str();                  // the layout the user builds is remembered

	ImGui::StyleColorsDark();
	ImGuiStyle &style = ImGui::GetStyle();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

	ImGui_ImplWin32_Init(window);
	ImGui_ImplDX11_Init(g_device, g_context);

	// Created here and released after the application is gone, which only draws it.
	ID3D11ShaderResourceView *logo = nullptr;
	{
		cygi::Application application(g_device, g_context);
		application.SetAdapterName(g_adapter_name);
		logo = LoadLogo(instance, g_device);
		application.SetLogo(logo);

		// --connect[=<pid>] attaches to a running session at start-up, and --level=<name> sets the
		// tracking level on it, so a demo launcher or a script does not need someone to click.
		// Without them nothing is connected and nothing is changed, as before.
		if (int count = 0; LPWSTR *arguments = CommandLineToArgvW(GetCommandLineW(), &count))
		{
			bool connected = false;
			for (int i = 1; i < count; ++i)
			{
				const std::wstring argument = arguments[i];
				if (!connected && argument.rfind(L"--connect", 0) == 0)
				{
					uint32_t process_id = 0;
					if (const size_t equals = argument.find(L'='); equals != std::wstring::npos)
						process_id = static_cast<uint32_t>(
							std::wcstoul(argument.c_str() + equals + 1, nullptr, 10));

					std::string message;
					application.ConnectToFirstSession(process_id, message);
					connected = true;
					continue;
				}

				// Deliberately after --connect in the same pass: setting a level means nothing
				// until there is a session to set it on, and the order on the line is the order
				// a person would expect.
				if (argument.rfind(L"--level=", 0) == 0)
				{
					const std::wstring name = argument.substr(8);
					const cygi::TrackingLevel level =
						name == L"idle" ? cygi::TrackingLevel::idle :
						name == L"tracking" ? cygi::TrackingLevel::tracking :
						name == L"pass-timing" ? cygi::TrackingLevel::pass_timing :
						name == L"capture" ? cygi::TrackingLevel::capture :
						name == L"full-draw-timing" ? cygi::TrackingLevel::full_draw_timing :
						cygi::TrackingLevel::tracking;

					std::string message;
					application.SetTrackingLevel(level, message);
				}
			}
			LocalFree(arguments);
		}

		auto previous = std::chrono::steady_clock::now();
		bool running = true;
		while (running)
		{
			MSG message;
			while (PeekMessage(&message, nullptr, 0U, 0U, PM_REMOVE))
			{
				TranslateMessage(&message);
				DispatchMessage(&message);
				if (message.message == WM_QUIT)
					running = false;
			}
			if (!running)
				break;

			// Do not burn a core while the window is hidden or occluded.
			if (g_swapchain_occluded && g_swapchain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
			{
				Sleep(10);
				continue;
			}
			g_swapchain_occluded = false;

			if (g_resize_width != 0 && g_resize_height != 0)
			{
				CleanupRenderTarget();
				g_swapchain->ResizeBuffers(0, g_resize_width, g_resize_height, DXGI_FORMAT_UNKNOWN, 0);
				g_resize_width = 0;
				g_resize_height = 0;
				CreateRenderTarget();
			}

			const auto now = std::chrono::steady_clock::now();
			const double delta = std::chrono::duration<double>(now - previous).count();
			previous = now;

			ImGui_ImplDX11_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			application.Draw(delta);
			if (application.WantsExit())
				running = false;

			ImGui::Render();
			constexpr float kClearColor[4] = { 0.09f, 0.09f, 0.11f, 1.0f };
			g_context->OMSetRenderTargets(1, &g_render_target, nullptr);
			g_context->ClearRenderTargetView(g_render_target, kClearColor);
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

			if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
			{
				ImGui::UpdatePlatformWindows();
				ImGui::RenderPlatformWindowsDefault();
			}

			const HRESULT present = g_swapchain->Present(1, 0);
			g_swapchain_occluded = present == DXGI_STATUS_OCCLUDED;
		}
	}

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	if (logo != nullptr)
		logo->Release();
	CleanupDeviceD3D();
	DestroyWindow(window);
	UnregisterClassW(window_class.lpszClassName, instance);
	return 0;
}
