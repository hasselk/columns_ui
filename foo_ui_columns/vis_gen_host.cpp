#include "stdafx.h"
#include "vis_gen_host.h"

#if 1
class window_visualisation_interface : public ui_extension::visualisation_host {
	service_ptr_t<window_visualisation> p_wnd;
public:
	class painter_impl : public uie::visualisation_host::painter_t {
		HDC m_dc;
		RECT m_rect;
		HGDIOBJ m_gdiobj;
		service_ptr_t<window_visualisation> m_wnd;
	public:
		HDC get_device_context() const override
		{
			return m_dc;
		};

		const RECT* get_area() const override
		{
			return &m_rect;
		};

		painter_impl(window_visualisation* p_wnd)
			: m_gdiobj(nullptr), m_wnd(p_wnd)
		{
			m_dc = CreateCompatibleDC(nullptr);
			if (!p_wnd->get_bitmap())
				p_wnd->make_bitmap();
			m_gdiobj = SelectObject(m_dc, p_wnd->get_bitmap());
			m_rect = *p_wnd->get_rect_client();
		}

		~painter_impl() override
		{
			HWND wnd = m_wnd->get_wnd();
			HDC dc = GetDC(wnd);
			BitBlt(dc, 0, 0, m_rect.right, m_rect.bottom, m_dc, 0, 0,SRCCOPY);
			SelectObject(m_dc, m_gdiobj);
			DeleteDC(m_dc);
			ReleaseDC(wnd, dc);
		};
	};

	void create_painter(painter_ptr& p_out) override
	{
		if (p_wnd->get_wnd()) {
			p_out = new painter_impl(p_wnd.get_ptr());
		}
	}

	static void g_create(service_ptr_t<window_visualisation_interface>& p_out, window_visualisation* wnd);

};

ui_extension::visualisation_host_factory<window_visualisation_interface> g_window_visualisation_interface;

void window_visualisation_interface::g_create(service_ptr_t<window_visualisation_interface>& p_out, window_visualisation* wnd)
{
	g_window_visualisation_interface.instance_create((service_ptr_t<service_base>&)p_out);
	p_out->p_wnd = wnd;
}

TCHAR* window_visualisation::class_name = _T("{ED4F644F-26AB-4aa0-809D-0D8F25352C5F}");

pfc::ptr_list_t<window_visualisation> window_visualisation::list_vis;


window_visualisation::window_visualisation() : initialised(false), m_frame(cfg_vis_edge), bm_display(nullptr), m_wnd(nullptr)
{
	memset(&rc_client, 0, sizeof(RECT));
};

window_visualisation::~window_visualisation() = default;


void window_visualisation::set_frame_style(unsigned p_type)
{
	m_frame = p_type;
	if (m_wnd) {
		long flags = WS_EX_CONTROLPARENT;
		if (m_frame == 1)
			flags |= WS_EX_CLIENTEDGE;
		if (m_frame == 2)
			flags |= WS_EX_STATICEDGE;

		SetWindowLongPtr(get_wnd(), GWL_EXSTYLE, flags);
		SetWindowPos(get_wnd(), nullptr, 0, 0, 0, 0,SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
	}
}

void window_visualisation::make_bitmap(HDC hdc)
{
	if (!bm_display) {
		//		RECT rc_client;
		//		GetClientRect(wnd_vis,&rc_client);

		HWND wnd_vis = get_wnd();
		HDC dc = hdc ? hdc : GetDC(wnd_vis);
		bm_display = CreateCompatibleBitmap(dc, rc_client.right, rc_client.bottom);
		HDC dcm = CreateCompatibleDC(nullptr);
		HGDIOBJ meh = SelectObject(dcm, bm_display);
		p_vis->paint_background(dcm, &rc_client);
		SelectObject(dcm, meh);
		DeleteDC(dcm);

		if (!hdc)
			ReleaseDC(wnd_vis, dc);
	}
}

void window_visualisation::flush_bitmap()
{
	if (bm_display) {
		DeleteObject(bm_display);
		bm_display = nullptr;
	}
}


LRESULT window_visualisation::on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {

		case WM_CREATE:
			{
				m_wnd = wnd;
				long flags = WS_EX_CONTROLPARENT;
				if (m_frame == 1)
					flags |= WS_EX_CLIENTEDGE;
				if (m_frame == 2)
					flags |= WS_EX_STATICEDGE;

				SetWindowLongPtr(wnd, GWL_EXSTYLE, flags);
				SetWindowPos(wnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);

				list_vis.add_item(this);
				initialised = true;

				ui_extension::visualisation::create_by_guid(get_visualisation_guid(), p_vis);
				if (p_vis.is_valid()) {
					GetClientRect(wnd, &rc_client);
					window_visualisation_interface::g_create(m_interface, this);
					try {
						abort_callback_dummy p_abort;
						p_vis->set_config_from_ptr(m_data.get_ptr(), m_data.get_size(), p_abort);
					}
					catch (const exception_io&) {};
					p_vis->enable(ui_extension::visualisation_host_ptr(m_interface.get_ptr()));
				}
				break;
			}
		case WM_DESTROY:
			{
				if (p_vis.is_valid())
					p_vis->disable();
				flush_bitmap();
				m_interface.release();
				initialised = false;
				list_vis.remove_item(this);
				m_wnd = nullptr;
				break;
			}
		case WM_PAINT:
			{
				if (p_vis.is_valid()) {
					RECT rc_paint;
					if (!GetUpdateRect(wnd, &rc_paint, 0)) {
						rc_paint = rc_client;
					}
					HDC dc = GetDC(wnd);
					HDC dc_bmp = CreateCompatibleDC(nullptr);
					if (!bm_display)
						make_bitmap(dc);
					HGDIOBJ meh = SelectObject(dc_bmp, bm_display);
					BitBlt(dc, 0, 0, rc_client.right, rc_client.bottom, dc_bmp, 0, 0, SRCCOPY);
					SelectObject(dc_bmp, meh);
					DeleteDC(dc_bmp);
					ReleaseDC(wnd, dc);
					ValidateRect(wnd, &rc_paint);
				}
				break;
			}
		case WM_GETMINMAXINFO:
			{
				LPMINMAXINFO mmi = LPMINMAXINFO(lp);
				mmi->ptMinTrackSize.x = uih::ScaleDpiValue(50);

				return 0;
			}
		case WM_WINDOWPOSCHANGED:
			{
				LPWINDOWPOS lpwp = (LPWINDOWPOS)lp;
				if (!(lpwp->flags & SWP_NOSIZE)) {
					GetClientRect(wnd, &rc_client);
					flush_bitmap();
				}
				break;
			}
		case WM_ERASEBKGND:
			return FALSE;
	}

	return DefWindowProc(wnd, msg, wp, lp);
}


void window_visualisation::get_name(pfc::string_base& out) const
{
	uie::visualization_ptr ptr;
	uie::visualization::create_by_guid(get_visualisation_guid(), ptr);
	ptr->get_name(out);
}

void window_visualisation::get_category(pfc::string_base& out) const
{
	out.set_string("Visualisations");
}

#endif
