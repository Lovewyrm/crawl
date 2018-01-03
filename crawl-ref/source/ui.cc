/**
 * @file
 * @brief Hierarchical layout system.
**/

#include "AppHdr.h"

#include <numeric>
#include <stack>

#include "ui.h"
#include "cio.h"

#ifdef USE_TILE_LOCAL
# include "glwrapper.h"
# include "tilebuf.h"
#else
# include "state.h"
# include "output.h"
# include "view.h"
# include "stringutil.h"
#endif

#define UI_DEBUG_DRAW 0

static i4 aabb_intersect(i4 a, i4 b)
{
    a[2] += a[0]; a[3] += a[1];
    b[2] += b[0]; b[3] += b[1];
    i4 i = { max(a[0], b[0]), max(a[1], b[1]), min(a[2], b[2]), min(a[3], b[3]) };
    i[2] -= i[0]; i[3] -= i[1];
    return i;
}

static i4 aabb_union(i4 a, i4 b)
{
    a[2] += a[0]; a[3] += a[1];
    b[2] += b[0]; b[3] += b[1];
    i4 i = { min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3]) };
    i[2] -= i[0]; i[3] -= i[1];
    return i;
}

static inline bool pos_in_rect(i2 pos, i4 rect)
{
    if (pos[0] < rect[0] || pos[0] >= rect[0]+rect[2])
        return false;
    if (pos[1] < rect[1] || pos[1] >= rect[1]+rect[3])
        return false;
    return true;
}

#ifndef USE_TILE_LOCAL
static void ui_clear_text_region(i4 region);
#endif

static struct UIRoot
{
public:
    UIRoot() : m_dirty_region({0, 0, 0, 0}), m_needs_layout(false) {};
    void push_child(shared_ptr<UI> child);
    void pop_child();

    void resize(int w, int h);
    void layout();
    void render();

    bool on_event(wm_event event);
    void queue_layout() { m_needs_layout = true; };
    void expose_region(i4 r) {
        if (r[2] == 0 || r[3] == 0)
            return;
        if (m_dirty_region[2] == 0)
            m_dirty_region = r;
        else
			m_dirty_region = aabb_union(m_dirty_region, r);
        needs_paint = true;
    };

    bool needs_paint;

protected:
    int m_w, m_h;
    i4 m_region;
    i4 m_dirty_region;
    UIStack m_root;
    bool m_needs_layout;
} ui_root;

static stack<i4> scissor_stack;

struct UI::slots UI::slots = {};

bool UI::on_event(wm_event event)
{
    if (event.type == WME_KEYDOWN || event.type == WME_KEYUP)
        return UI::slots.event.emit(this, event);
    else
        return false;
}

static inline bool _maybe_propagate_event(wm_event event, shared_ptr<UI> &child)
{
#ifdef USE_TILE_LOCAL
    if (event.type == WME_MOUSEMOTION)
    {
        i2 pos = i2((int)event.mouse_event.px, (int)event.mouse_event.py);
        if (!pos_in_rect(pos, child->get_region()))
            return false;
    }
#endif
    return child->on_event(event);
}

bool UIContainer::on_event(wm_event event)
{
    if (UI::on_event(event))
        return true;
    for (shared_ptr<UI> &child : *this)
        if (_maybe_propagate_event(event, child))
            return true;
    return false;
}

bool UIBin::on_event(wm_event event)
{
    if (UI::on_event(event))
        return true;
    if (_maybe_propagate_event(event, m_child))
        return true;
    return false;
}

void UI::render()
{
#ifdef USE_TILE_LOCAL
#if UI_DEBUG_DRAW
    LineBuffer buf;
    const VColour col_region(160, 80, 80, 255);
    const VColour col_margin(80, 160, 80, 255);
    buf.add_square(m_region[0], m_region[1],
            m_region[0] + m_region[2]-1, m_region[1] + m_region[3]-1, col_region);
    buf.add_square(m_region[0]-margin[3], m_region[1]-margin[0],
            m_region[0] + m_region[2] + margin[1]-1,
            m_region[1] + m_region[3] + margin[2]-1, col_margin);
    buf.draw();
#endif
#endif
    _render();
}

UISizeReq UI::get_preferred_size(int dim, int prosp_width)
{
    ASSERT(dim == 0 || dim == 1);
    ASSERT((dim == 0) == (prosp_width == -1));

    if (cached_sr_valid[dim] && (!dim || cached_sr_pw == prosp_width))
        return cached_sr[dim];

    prosp_width = dim ? prosp_width - margin[1] - margin[3] : prosp_width;
    UISizeReq ret = _get_preferred_size(dim, prosp_width);
    ASSERT(ret.min <= ret.nat);
    int m = margin[1-dim] + margin[3-dim];
    ret.min += m;
    ret.nat += m;

    const int ui_expand_sz = 0xffffff;
    if (dim ? expand_v : expand_h)
        ret.nat = ui_expand_sz;
    ret.nat = min(ret.nat, ui_expand_sz);

    cached_sr_valid[dim] = true;
    cached_sr[dim] = ret;
    if (dim)
        cached_sr_pw = prosp_width;

    return ret;
}

void UI::allocate_region(i4 region)
{
    i4 new_region = {
        region[0] + margin[3],
        region[1] + margin[0],
        region[2] - margin[3] - margin[1],
        region[3] - margin[0] - margin[2],
    };

    if (m_region == new_region && !alloc_queued)
        return;
    ui_root.expose_region(m_region);
    ui_root.expose_region(new_region);
    m_region = new_region;
    alloc_queued = false;

    ASSERT(m_region[2] >= 0);
    ASSERT(m_region[3] >= 0);
    _allocate_region();
}

UISizeReq UI::_get_preferred_size(int dim, int prosp_width)
{
    UISizeReq ret = { 0, 0 };
    return ret;
}

void UI::_allocate_region()
{
}

void UI::_set_parent(UI* p)
{
    m_parent = p;
}

void UI::_invalidate_sizereq()
{
    cached_sr_valid[0] = false;
    cached_sr_valid[1] = false;
    if (m_parent)
        m_parent->_invalidate_sizereq();
    ui_root.queue_layout();
}

void UI::_queue_allocation()
{
    if (alloc_queued)
        return;
    alloc_queued = true;
    if (m_parent)
        m_parent->_queue_allocation();
    else
        ui_root.queue_layout();
}

void UIBox::add_child(shared_ptr<UI> child)
{
    child->_set_parent(this);
    m_children.push_back(move(child));
    _invalidate_sizereq();
}

void UIBox::_render()
{
    for (auto const& child : m_children)
        child->render();
}

vector<int> UIBox::layout_main_axis(vector<UISizeReq>& ch_psz, int main_sz)
{
    // find the child sizes on the main axis
    vector<int> ch_sz(m_children.size());

    int extra = main_sz;
    for (size_t i = 0; i < m_children.size(); i++)
    {
        ch_sz[i] = ch_psz[i].min;
        extra -= ch_psz[i].min;
    }
    ASSERT(extra >= 0);

    while (extra > 0)
    {
        int sum_flex_grow = 0, remainder = 0;
        for (size_t i = 0; i < m_children.size(); i++)
        {
            sum_flex_grow += ch_sz[i] < ch_psz[i].nat ? m_children[i]->flex_grow : 0;
        }
        if (!sum_flex_grow)
            break;

        // distribute space to children, based on flex_grow
        for (size_t i = 0; i < m_children.size(); i++)
        {
            float efg = ch_sz[i] < ch_psz[i].nat ? m_children[i]->flex_grow : 0;
            int ch_extra = extra * efg / sum_flex_grow;
            ASSERT(ch_sz[i] <= ch_psz[i].nat);
            int taken = min(ch_extra, ch_psz[i].nat - ch_sz[i]);
            ch_sz[i] += taken;
            remainder += ch_extra - taken;
        }
        extra = remainder;
    }

    return ch_sz;
}

vector<int> UIBox::layout_cross_axis(vector<UISizeReq>& ch_psz, int cross_sz)
{
    vector<int> ch_sz(m_children.size());

    for (size_t i = 0; i < m_children.size(); i++)
    {
        auto const& child = m_children[i];
        // find the child's size on the cross axis
        bool stretch = child->align_self == UI_ALIGN_STRETCH ? true
            : align_items == UI_ALIGN_STRETCH;
        ch_sz[i] = stretch ? cross_sz : min(max(ch_psz[i].min, cross_sz), ch_psz[i].nat);
    }

    return ch_sz;
}

UISizeReq UIBox::_get_preferred_size(int dim, int prosp_width)
{
    vector<UISizeReq> sr(m_children.size());

    // Get preferred widths
    for (size_t i = 0; i < m_children.size(); i++)
        sr[i] = m_children[i]->get_preferred_size(0, -1);

    if (dim)
    {
        // Get actual widths
        vector<int> cw = horz ? layout_main_axis(sr, prosp_width) : layout_cross_axis(sr, prosp_width);

        // Get preferred heights
        for (size_t i = 0; i < m_children.size(); i++)
            sr[i] = m_children[i]->get_preferred_size(1, cw[i]);
    }

    // find sum/max of preferred sizes, as appropriate
    bool main_axis = dim == !horz;
    UISizeReq r = { 0, 0 };
    for (auto const& c : sr)
    {
        r.min = main_axis ? r.min + c.min : max(r.min, c.min);
        r.nat = main_axis ? r.nat + c.nat : max(r.nat, c.nat);
    }
    return r;
}

void UIBox::_allocate_region()
{
    vector<UISizeReq> sr(m_children.size());

    // Get preferred widths
    for (size_t i = 0; i < m_children.size(); i++)
        sr[i] = m_children[i]->get_preferred_size(0, -1);

    // Get actual widths
    vector<int> cw = horz ? layout_main_axis(sr, m_region[2]) : layout_cross_axis(sr, m_region[2]);

    // Get preferred heights
    for (size_t i = 0; i < m_children.size(); i++)
        sr[i] = m_children[i]->get_preferred_size(1, cw[i]);

    // Get actual heights
    vector<int> ch = horz ? layout_cross_axis(sr, m_region[3]) : layout_main_axis(sr, m_region[3]);

    auto const &m = horz ? cw : ch;
    int extra_main_space = m_region[horz ? 2 : 3] - accumulate(m.begin(), m.end(), 0);
    ASSERT(extra_main_space >= 0);

    // main axis offset
    int mo = extra_main_space*(justify_items - UI_JUSTIFY_START)/2;
    int ho = m_region[0] + (horz ? mo : 0);
    int vo = m_region[1] + (!horz ? mo : 0);

    i4 cr = {ho, vo, 0, 0};
    for (size_t i = 0; i < m_children.size(); i++)
    {
        // cross axis offset
        int extra_cross_space = horz ? m_region[3] - ch[i] : m_region[2] - cw[i];
        int xp = horz ? 1 : 0, xs = xp + 2;

        auto const& child = m_children[i];
        UIAlign_type child_align = child->align_self ? child->align_self
                : align_items ? align_items
                : UI_ALIGN_START;
        int xo;
        switch (child_align)
        {
            case UI_ALIGN_START:   xo = 0; break;
            case UI_ALIGN_CENTER:  xo = extra_cross_space/2; break;
            case UI_ALIGN_END:     xo = extra_cross_space; break;
            case UI_ALIGN_STRETCH: xo = 0; break;
            default: ASSERT(0);
        }
        cr[xp] = (horz ? vo : ho) + xo;

        cr[2] = cw[i];
        cr[3] = ch[i];
        if (child_align == UI_ALIGN_STRETCH)
            cr[xs] = (horz ? ch : cw)[i];
        m_children[i]->allocate_region(cr);
        cr[horz ? 0 : 1] += cr[horz ? 2 : 3];
    }
}

void UIText::set_text(const formatted_string &fs)
{
    m_text.clear();
    m_text += fs;
    _invalidate_sizereq();
    ui_root.expose_region(m_region);
    m_wrapped_size = { -1, -1 };
    _queue_allocation();
}

void UIText::wrap_text_to_size(int width, int height)
{
    i2 wrapped_size = { width, height };
    if (m_wrapped_size == wrapped_size)
        return;
    m_wrapped_size = wrapped_size;

    height = height ? height : 0xfffffff;

#ifdef USE_TILE_LOCAL
    if (wrap_text || ellipsize)
        m_text_wrapped = tiles.get_crt_font()->split(m_text, width, height);
    else
        m_text_wrapped = m_text;

    m_brkpts.clear();
    m_brkpts.emplace_back(brkpt({0, 0}));
    unsigned tally = 0, acc = 0;
    for (unsigned i = 0; i < m_text_wrapped.ops.size(); i++)
    {
        formatted_string::fs_op &op = m_text_wrapped.ops[i];
        if (op.type != FSOP_TEXT)
            continue;
        if (acc > 0)
        {
            m_brkpts.emplace_back(brkpt({i, tally}));
            acc = 0;
        }
        unsigned n = count(op.text.begin(), op.text.end(), '\n');
        acc += n;
        tally += n;
    }
#else
    m_wrapped_lines.clear();
    formatted_string::parse_string_to_multiple(m_text.to_colour_string(), m_wrapped_lines, width);
    // add ellipsis to last line of text if necessary
    if (height < (int)m_wrapped_lines.size())
    {
        auto& last_line = m_wrapped_lines[height-1], next_line = m_wrapped_lines[height];
        last_line += formatted_string(" ");
        last_line += next_line;
        last_line = last_line.chop(width-2);
        last_line += formatted_string("..");
        m_wrapped_lines.resize(height);
    }
#endif
}

void UIText::_render()
{
    i4 region = m_region;
    if (scissor_stack.size() > 0)
        region = aabb_intersect(region, scissor_stack.top());
    if (region[2] <= 0 || region[3] <= 0)
        return;

#ifdef USE_TILE_LOCAL
    const int line_height = tiles.get_crt_font()->char_height();
    const unsigned line_min = (region[1]-m_region[1]) / line_height;
    const unsigned line_max = (region[1]+region[3]-m_region[1]) / line_height;

    int line_off = 0;
    int ops_min = 0, ops_max = m_text_wrapped.ops.size(), i = 1;
    for (; i < (int)m_brkpts.size(); i++)
        if (m_brkpts[i].line >= line_min)
        {
            ops_min = m_brkpts[i-1].op;
            line_off = m_brkpts[i-1].line;
            break;
        }
    for (; i < (int)m_brkpts.size(); i++)
        if (m_brkpts[i].line > line_max)
        {
            ops_max = m_brkpts[i].op;
            break;
        }

    formatted_string slice;
    slice.ops = vector<formatted_string::fs_op>(
        m_text_wrapped.ops.begin()+ops_min,
        m_text_wrapped.ops.begin()+ops_max);

    // XXX: should be moved into a new function render_formatted_string()
    // in FTFontWrapper, that, like render_textblock(), would automatically
    // handle swapping atlas glyphs as necessary.
    FontBuffer m_font_buf(tiles.get_crt_font());
    m_font_buf.add(slice, m_region[0], m_region[1]+line_height*line_off);
    m_font_buf.draw();
#else
    const auto& lines = m_wrapped_lines;
    for (size_t i = 0; i < min(lines.size(), (long unsigned)region[3]); i++)
    {
        cgotoxy(region[0]+1, region[1]+1+i);
        lines[i+region[1]-m_region[1]].chop(region[2]).display(0);
    }
#endif
}

UISizeReq UIText::_get_preferred_size(int dim, int prosp_width)
{
#ifdef USE_TILE_LOCAL
    FontWrapper *font = tiles.get_crt_font();
    if (!dim)
    {
        int w = font->string_width(m_text);
        static constexpr int min_ellipsized_width = 4; // XXX: should be width of '..'
        static constexpr int min_wrapped_width = 4; // XXX: should be width of longest word
        return { ellipsize ? min_ellipsized_width : wrap_text ? min_wrapped_width : w, w };
    }
    else
    {
        wrap_text_to_size(prosp_width, 0);
        int height = font->string_height(m_text_wrapped);
        return { ellipsize ? (int)font->char_height() : height, height };
    }
#else
    if (!dim)
    {
        int w = 0, line_w = 0;
        for (auto const& ch : m_text.tostring())
        {
            w = ch == '\n' ? max(w, line_w) : w;
            line_w = ch == '\n' ? 0 : line_w+1;
        }
        w = max(w, line_w);

        static constexpr int min_ellipsized_width = 2; // strlen("..");
        static constexpr int min_wrapped_width = 4; // XXX: should be char width of longest word in text
        return { ellipsize ? min_ellipsized_width : wrap_text ? min_wrapped_width : w, w };
    }
    else
    {
        wrap_text_to_size(prosp_width, 0);
        int height = m_wrapped_lines.size();
        return { ellipsize ? 1 : height, height };
    }
#endif
}

void UIText::_allocate_region()
{
    wrap_text_to_size(m_region[2], m_region[3]);
}

void UIImage::set_tile(tile_def tile)
{
#ifdef USE_TILE_LOCAL
    m_tile = tile;
    const tile_info &ti = tiles.get_image_manager()->tile_def_info(m_tile);
    m_tw = ti.width;
    m_th = ti.height;
    m_using_tile = true;
    _invalidate_sizereq();
#endif
}

void UIImage::set_file(string img_path)
{
#ifdef USE_TILE_LOCAL
    m_using_tile = false;

    if (!m_img.load_texture(img_path.c_str(), MIPMAP_NONE))
        return;

    m_tw = m_img.orig_width();
    m_th = m_img.orig_height();
    _invalidate_sizereq();
#endif
}

void UIImage::_render()
{
#ifdef USE_TILE_LOCAL
    ui_push_scissor(m_region);
    if (m_using_tile)
    {
        TileBuffer tb;
        tb.set_tex(&tiles.get_image_manager()->m_textures[m_tile.tex]);

        for (int y = m_region[1]; y < m_region[1]+m_region[3]; y+=m_th)
            for (int x = m_region[0]; x < m_region[0]+m_region[2]; x+=m_tw)
            {
                tb.add(m_tile.tile, x, y, 0, 0, false, m_th, 1.0, 1.0);
            }

        tb.draw();
        tb.clear();
    }
    else
    {
        VertBuffer buf(true, false);
        buf.set_tex(&m_img);

        for (int y = m_region[1]; y < m_region[1]+m_region[3]; y+=m_th)
            for (int x = m_region[0]; x < m_region[0]+m_region[2]; x+=m_tw)
            {
                GLWPrim rect(x, y, x+m_tw, y+m_th);
                rect.set_tex(0, 0, (float)m_img.orig_width()/m_img.width(),
                        (float)m_img.orig_height()/m_img.height());
                buf.add_primitive(rect);
            }

        buf.draw();
    }
    ui_pop_scissor();
#endif
}

UISizeReq UIImage::_get_preferred_size(int dim, int prosp_width)
{
#ifdef USE_TILE_LOCAL
    UISizeReq ret = {
        // This is a little ad-hoc, but expand taking precedence over shrink when
        // determining the natural size makes the textured dialog box work
        dim ? (shrink_v ? 0 : m_th) : (shrink_h ? 0 : m_tw),
        dim ? (shrink_v ? 0 : m_th) : (shrink_h ? 0 : m_tw)
    };
#else
    UISizeReq ret = { 0, 0 };
#endif
    return ret;
}

void UIStack::add_child(shared_ptr<UI> child)
{
    child->_set_parent(this);
    m_children.push_back(move(child));
    _invalidate_sizereq();
    _queue_allocation();
}

void UIStack::pop_child()
{
    if (!m_children.size())
        return;
    m_children.pop_back();
    _invalidate_sizereq();
    _queue_allocation();
}

void UIStack::_render()
{
    for (auto const& child : m_children)
        child->render();
}

UISizeReq UIStack::_get_preferred_size(int dim, int prosp_width)
{
    UISizeReq r = { 0, 0 };
    for (auto const& child : m_children)
    {
        UISizeReq c = child->get_preferred_size(dim, prosp_width);
        r.min = max(r.min, c.min);
        r.nat = max(r.nat, c.nat);
    }
    return r;
}

void UIStack::_allocate_region()
{
    for (auto const& child : m_children)
    {
        i4 cr = m_region;
        UISizeReq pw = child->get_preferred_size(0, -1);
        cr[2] = min(max(pw.min, m_region[2]), pw.nat);
        UISizeReq ph = child->get_preferred_size(1, cr[2]);
        cr[3] = min(max(ph.min, m_region[3]), ph.nat);
        child->allocate_region(cr);
    }
}

bool UIStack::on_event(wm_event event)
{
    if (UI::on_event(event))
        return true;
    if (m_children.size() > 0 &&_maybe_propagate_event(event, m_children.back()))
        return true;
    return false;
}

void UIGrid::add_child(shared_ptr<UI> child, int x, int y, int w, int h)
{
    child->_set_parent(this);
    child_info ch = { {x, y}, {w, h}, move(child) };
    m_child_info.push_back(ch);
    m_track_info_dirty = true;
    _invalidate_sizereq();
}

void UIGrid::init_track_info()
{
    if (!m_track_info_dirty)
        return;
    m_track_info_dirty = false;

    // calculate the number of rows and columns
    int n_rows = 0, n_cols = 0;
    for (auto info : m_child_info)
    {
        n_rows = max(n_rows, info.pos[1]+info.span[1]);
        n_cols = max(n_cols, info.pos[0]+info.span[0]);
    }
    m_row_info.resize(n_rows);
    m_col_info.resize(n_cols);

    sort(m_child_info.begin(), m_child_info.end(), [](const child_info& a, const child_info& b) {
        // XXX: sorting by colspan is a hack to get menu entry backgrounds drawn
        // before menu text widgets. Alternatives: add z-index property to widgets,
        // or make render() call order not matter by deferring rendering
        return tie(a.pos[1], b.span[0]) < tie(b.pos[1], a.span[0]);
    });
}

void UIGrid::_render()
{
    // Find the visible rows
    i4 scissor = ui_get_scissor();
    int row_min = 0, row_max = m_row_info.size()-1, i = 0;
    for (; i < (int)m_row_info.size(); i++)
        if (m_row_info[i].offset+m_row_info[i].size+m_region[1] >= scissor[1])
        {
            row_min = i;
            break;
        }
    for (; i < (int)m_row_info.size(); i++)
        if (m_row_info[i].offset+m_region[1] >= scissor[1]+scissor[3])
        {
            row_max = i-1;
            break;
        }

    for (auto const& child : m_child_info)
    {
        if (child.pos[1] < row_min) continue;
        if (child.pos[1] > row_max) break;
        child.widget->render();
    }
}

void UIGrid::compute_track_sizereqs(int dim)
{
    auto& track = dim ? m_row_info : m_col_info;
#define DIV_ROUND_UP(n,d) (((n)+(d)-1)/(d))

    for (auto& t : track)
        t.sr = {0, 0};
    for (size_t i = 0; i < m_child_info.size(); i++)
    {
        auto& cp = m_child_info[i].pos, cs = m_child_info[i].span;
        // if merging horizontally, need to find (possibly multi-col) width
        int prosp_width = dim ? get_tracks_region(cp[0], cp[1], cs[0], cs[1])[2] : -1;

        const UISizeReq c = m_child_info[i].widget->get_preferred_size(dim, prosp_width);
        // crappy but fast multitrack distribution here: if a child spans n tracks
        // each track gets 1/n-th of its sizereq; good enough for our needs
        for (int ti = cp[dim]; ti < cp[dim]+cs[dim]; ti++)
        {
            auto& s = track[ti].sr;
            s.min = max(s.min, DIV_ROUND_UP(c.min, cs[dim]));
            s.nat = max(s.nat, DIV_ROUND_UP(c.nat, cs[dim]));
        }
    }
}

void UIGrid::set_track_offsets(vector<track_info>& tracks)
{
    int acc = 0;
    for(auto& track : tracks)
    {
        track.offset = acc;
        acc += track.size;
    }
}

UISizeReq UIGrid::_get_preferred_size(int dim, int prosp_width)
{
    init_track_info();

    // get preferred column widths
    compute_track_sizereqs(0);

    // total width min and nat
    UISizeReq w_sr = { 0, 0 };
    for (auto const& col : m_col_info)
    {
        w_sr.min += col.sr.min;
        w_sr.nat += col.sr.nat;
    }

    if (!dim)
        return w_sr;

    layout_track(0, w_sr, prosp_width);
    set_track_offsets(m_col_info);

    // get preferred row heights for those widths
    compute_track_sizereqs(1);

    // total height min and nat
    UISizeReq h_sr = { 0, 0 };
    for (auto const& row : m_row_info)
    {
        h_sr.min += row.sr.min;
        h_sr.nat += row.sr.nat;
    }

    return h_sr;
}

void UIGrid::layout_track(int dim, UISizeReq sr, int size)
{
    auto& infos = dim ? m_row_info : m_col_info;

    int extra = size - sr.min;
    ASSERT(extra >= 0);

    for(size_t i = 0; i < infos.size(); ++i)
        infos[i].size = infos[i].sr.min;

    while (extra > 0)
    {
        int sum_flex_grow = 0, remainder = 0;
        for (const auto& info : infos)
            sum_flex_grow += info.size < info.sr.nat ? info.flex_grow : 0;
        if (!sum_flex_grow)
            break;

        for(size_t i = 0; i < infos.size(); ++i)
        {
            float efg = infos[i].size < infos[i].sr.nat ? infos[i].flex_grow : 0;
            int tr_extra = extra * efg / sum_flex_grow;
            ASSERT(infos[i].size <= infos[i].sr.nat);
            int taken = min(tr_extra, infos[i].sr.nat - infos[i].size);
            infos[i].size += taken;
            remainder += tr_extra - taken;
        }
        extra = remainder;
    }
}

void UIGrid::_allocate_region()
{
    // Use of _-prefixed member function is necessary here
    UISizeReq h_sr = _get_preferred_size(1, m_region[2]);

    layout_track(1, h_sr, m_region[3]);
    set_track_offsets(m_row_info);

    for (size_t i = 0; i < m_child_info.size(); i++)
    {
        auto& cp = m_child_info[i].pos, cs = m_child_info[i].span;
        i4 cell_reg = get_tracks_region(cp[0], cp[1], cs[0], cs[1]);
        cell_reg[0] += m_region[0];
        cell_reg[1] += m_region[1];
        m_child_info[i].widget->allocate_region(cell_reg);
    }
}

void UIScroller::set_scroll(int y)
{
    if (m_scroll == y)
        return;
    m_scroll = y;
    _queue_allocation();
}

void UIScroller::set_child(shared_ptr<UI> child)
{
    child->_set_parent(this);
    m_child = move(child);
    _invalidate_sizereq();
}

void UIScroller::_render()
{
    if (m_child)
    {
        ui_push_scissor(m_region);
        m_child->render();
        ui_pop_scissor();
    }
}

UISizeReq UIScroller::_get_preferred_size(int dim, int prosp_width)
{
    if (!m_child)
        return { 0, 0 };

    UISizeReq sr = m_child->get_preferred_size(dim, prosp_width);
    if (dim) sr.min = 0; // can shrink to zero height
    return sr;
}

void UIScroller::_allocate_region()
{
    UISizeReq sr = m_child->get_preferred_size(1, m_region[2]);
    m_scroll = max(0, min(m_scroll, sr.nat-m_region[3]));
    i4 ch_reg = {m_region[0], m_region[1]-m_scroll, m_region[2], sr.nat};
    m_child->allocate_region(ch_reg);
}

bool UIScroller::on_event(wm_event event)
{
    if (UIBin::on_event(event))
        return true;
#ifdef USE_TILE_LOCAL
    const int line_delta = 20;
#else
    const int line_delta = 1;
#endif
    int delta = 0;
    if (event.type == WME_KEYDOWN)
    {
        switch (event.key.keysym.sym)
        {
            case ' ': case '+': case CK_PGDN: case '>': case '\'':
                delta = m_region[3];
                break;

            case '-': case CK_PGUP: case '<': case ';':
                delta = -m_region[3];
                break;

            case CK_UP:
                delta = -line_delta;
                break;

            case CK_DOWN:
            case CK_ENTER:
                delta = line_delta;
                break;

            case CK_HOME:
                set_scroll(0);
                return true;

            case CK_END:
                set_scroll(INT_MAX);
                return true;
        }
    }
    else if (event.type == WME_MOUSEWHEEL)
        delta = event.mouse_event.py * line_delta;
    else if (event.type == WME_MOUSEBUTTONDOWN && event.mouse_event.button == MouseEvent::LEFT)
        delta = line_delta;
    if (delta != 0)
    {
        set_scroll(m_scroll+delta);
        return true;
    }
    return false;
}

UIPopup::UIPopup(shared_ptr<UI> child)
{
    vector<shared_ptr<UIImage>> box_img(9);
    for (int i=0; i<9; i++)
    {
        box_img[i] = make_shared<UIImage>();
        box_img[i]->set_tile(tile_def(TILEG_SKIN_BOX+i, TEX_GUI));
        box_img[i]->shrink_h = i==1 || i == 4 || i==7;
        box_img[i]->shrink_v = i==3 || i == 4 || i==5;
    }

    auto box_grid = make_shared<UIGrid>();
    for(int y = 0; y < 3; y++)
        for(int x = 0; x < 3; x++)
            box_grid->add_child(move(box_img[y*3+x]), x+1, y+1);
    box_grid->add_child(child, 2, 2);

    // XXX: Add struts around the box, because UIGrid barfs with empty tracks
#ifdef USE_TILE_LOCAL
    const bool expand_struts = true;
#else
    const bool expand_struts = false;
#endif
    box_grid->add_child(make_shared<UIBox>(false, expand_struts, false), 0, 2);
    box_grid->add_child(make_shared<UIBox>(false, expand_struts, false), 4, 2);
    box_grid->add_child(make_shared<UIBox>(false, false, expand_struts), 2, 0);
    box_grid->add_child(make_shared<UIBox>(false, false, expand_struts), 2, 4);

    box_grid->set_margin_for_sdl({15, 15, 15, 15});
    box_grid->track_flex_grow(0, -1) = 1;
    box_grid->track_flex_grow(2, -1) = 1000;
    box_grid->track_flex_grow(4, -1) = 1;
    box_grid->track_flex_grow(-1, 0) = 1;
    box_grid->track_flex_grow(-1, 2) = 1000;
    box_grid->track_flex_grow(-1, 4) = 1;

    m_child = move(child);
    m_root = move(box_grid);
    m_root->_set_parent(this);
}

void UIPopup::_render()
{
#ifdef USE_TILE_LOCAL
    m_buf.draw();
#endif
    m_root->render();
}

UISizeReq UIPopup::_get_preferred_size(int dim, int prosp_width)
{
    return m_root->get_preferred_size(dim, prosp_width);
}

void UIPopup::_allocate_region()
{
#ifdef USE_TILE_LOCAL
    m_buf.clear();
    m_buf.add(m_region[0], m_region[1],
            m_region[0] + m_region[2], m_region[1] + m_region[3],
            VColour(0, 0, 0, 150));
#endif
    m_root->allocate_region(m_region);
}

#ifdef USE_TILE_LOCAL
void UIDungeon::_render()
{
#ifdef USE_TILE_LOCAL
    GLW_3VF t = {(float)m_region[0], (float)m_region[1], 0}, s = {32, 32, 1};
    glmanager->set_transform(t, s);
    m_buf.draw();
    glmanager->reset_transform();
#endif
}

UISizeReq UIDungeon::_get_preferred_size(int dim, int prosp_width)
{
    int sz = (dim ? height : width)*32;
    return {sz, sz};
}
#endif

void UIRoot::push_child(shared_ptr<UI> ch)
{
    m_root.add_child(move(ch));
    m_needs_layout = true;
#ifndef USE_TILE_LOCAL
    if (m_root.num_children() == 1)
    {
        clrscr();
        ui_root.resize(get_number_of_cols(), get_number_of_lines());
    }
#endif
}

void UIRoot::pop_child()
{
    m_root.pop_child();
    m_needs_layout = true;
#ifndef USE_TILE_LOCAL
    if (m_root.num_children() == 0)
        clrscr();
#endif
}

void UIRoot::resize(int w, int h)
{
    if (w == m_w && h == m_h)
        return;

    m_w = w;
    m_h = h;
    m_needs_layout = true;

    // On console with the window size smaller than the minimum layout,
    // enlarging the window will not cause any size reallocations, and the
    // newly visible region of the terminal will not be filled.
    // Fix: explicitly mark the entire screen as dirty on resize: it won't
    // be strictly necessary for most resizes, but won't hurt.
#ifndef USE_TILE_LOCAL
    expose_region({0, 0, w, h});
#endif
}

void UIRoot::layout()
{
    while (m_needs_layout)
    {
        m_needs_layout = false;

        // Find preferred size with height-for-width: we never allocate less than
        // the minimum size, but may allocate more than the natural size.
        UISizeReq sr_horz = m_root.get_preferred_size(0, -1);
        int width = max(sr_horz.min, m_w);
        UISizeReq sr_vert = m_root.get_preferred_size(1, width);
        int height = max(sr_vert.min, m_h);

#ifdef USE_TILE_LOCAL
        m_region = {0, 0, width, height};
#else
        m_region = {0, 0, m_w, m_h};
#endif
        m_root.allocate_region({0, 0, width, height});
    }
}

void UIRoot::render()
{
    if (!needs_paint)
        return;

#ifdef USE_TILE_LOCAL
    glmanager->reset_view_for_redraw(0, 0);
    tiles.render_current_regions();
    glmanager->reset_transform();
#else
    // On console, clear and redraw only the dirty region of the screen
    m_dirty_region = aabb_intersect(m_dirty_region, m_region);
    textcolour(LIGHTGREY);
    textbackground(BLACK);
    ui_clear_text_region(m_dirty_region);
#endif

    ui_push_scissor(m_region);
#ifdef USE_TILE_LOCAL
    m_root.render();
#else
    // Render only the top of the UI stack on console
    if (m_root.num_children() > 0)
        m_root.get_child(m_root.num_children()-1)->render();
    else
        redraw_screen(false);
#endif
    ui_pop_scissor();

#ifdef USE_TILE_LOCAL
    wm->swap_buffers();
#else
    update_screen();
#endif

    needs_paint = false;
    m_dirty_region = {0, 0, 0, 0};
}

bool UIRoot::on_event(wm_event event)
{
    return m_root.on_event(event);
}

void ui_push_scissor(i4 scissor)
{
    if (scissor_stack.size() > 0)
        scissor = aabb_intersect(scissor, scissor_stack.top());
    scissor_stack.push(scissor);
#ifdef USE_TILE_LOCAL
    glmanager->set_scissor(scissor[0], scissor[1], scissor[2], scissor[3]);
#endif
}

void ui_pop_scissor()
{
    ASSERT(scissor_stack.size() > 0);
    scissor_stack.pop();
#ifdef USE_TILE_LOCAL
    if (scissor_stack.size() > 0)
    {
        i4 scissor = scissor_stack.top();
        glmanager->set_scissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    }
    else
        glmanager->reset_scissor();
#endif
}

i4 ui_get_scissor()
{
    return scissor_stack.top();
}

#ifndef USE_TILE_LOCAL
static void ui_clear_text_region(i4 region)
{
    if (scissor_stack.size() > 0)
        region = aabb_intersect(region, scissor_stack.top());
    if (region[2] <= 0 || region[3] <= 0)
        return;
    textcolour(LIGHTGREY);
    textbackground(BLACK);
    for (int y=region[1]; y < region[1]+region[3]; y++)
    {
        cgotoxy(region[0]+1, y+1);
        cprintf("%*s", region[2], "");
    }
}
#endif

void ui_push_layout(shared_ptr<UI> root)
{
    ui_root.push_child(move(root));
}

void ui_pop_layout()
{
    ui_root.pop_child();
}

void ui_resize(int w, int h)
{
    ui_root.resize(w, h);
}

void ui_pump_events()
{
#ifdef USE_TILE_LOCAL
    // Don't render while there are unhandled mousewheel events,
    // since these can come in faster than crawl can redraw.
    // unlike mousemotion events, we don't drop all but the last event
    if (!wm->get_event_count(WME_MOUSEWHEEL))
#endif
    {
        ui_root.layout();
        ui_root.render();
    }

#ifdef USE_TILE_LOCAL
    wm_event event = {0};
    while (true)
    {
        if (!wm->wait_event(&event))
            continue;
        if (event.type == WME_MOUSEMOTION)
        {
            // For consecutive mouse events, ignore all but the last,
            // since these can come in faster than crawl can redraw.
            //
            // Note that get_event_count() is misleadingly named and only
            // peeks at the first event, and so will only return 0 or 1.
            if (wm->get_event_count(WME_MOUSEMOTION) > 0)
                continue;
        }
        if (event.type == WME_KEYDOWN && event.key.keysym.sym == 0)
            continue;
        break;
    }

    switch (event.type)
    {
        case WME_RESIZE:
        {
            ui_root.resize(event.resize.w, event.resize.h);
            tiles.resize_event(event.resize.w, event.resize.h);
            break;
        }

        case WME_EXPOSE:
            ui_root.needs_paint = true;
            break;

        default:
            ui_root.on_event(event);
            break;
    }
#else
    set_getch_returns_resizes(true);
    int k = getch_ck();
    set_getch_returns_resizes(false);

    if (k == CK_RESIZE)
    {
        // This may be superfluous, since the resize handler may have already
        // resized the screen
        clrscr();
        console_shutdown();
        console_startup();
        ui_root.resize(get_number_of_cols(), get_number_of_lines());
    }
    else
    {
        wm_event ev = {0};
        ev.type = WME_KEYDOWN;
        ev.key.keysym.sym = k;
        ui_root.on_event(ev);
    }
#endif
}