#include "ui/markdown.h"

#include "libs/md4c.h"
#include "ui/internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MD_TABLE_MAX_ROWS 64
#define MD_TABLE_MAX_COLS 12
#define MD_TABLE_CELL_MAX 256

typedef enum {
  LINK_NONE,
  LINK_TEXT,
  LINK_IMAGE_ALT,
} LinkMode;

typedef struct {
  int quote_depth;
  int list_depth;
  int ordered_stack[16];
  int item_stack[16];
  int heading_level;
  int table_cell;
  int table_header_cols;
  int table_row;
  int table_cols;
  int table_header_rows;
  bool table_row_is_header;
  char code_lang[64];
  bool at_line_start;
  bool in_code_block;
  bool code_line_open;
  bool in_table;
  bool in_table_head;
  bool in_table_cell;
  bool pending_li_marker;
  bool need_block_gap;
  bool paragraph_open;
  bool span_code;
  bool span_del;
  LinkMode link_mode;
  char link_href[1024];
  char image_src[1024];
  char image_alt[512];
  size_t image_alt_len;
  char table_cells[MD_TABLE_MAX_ROWS][MD_TABLE_MAX_COLS][MD_TABLE_CELL_MAX];
  bool table_header[MD_TABLE_MAX_ROWS];
} MdRender;

static void newline(MdRender *r);
static void start_line(MdRender *r);

static bool out_color(void) { return isatty(STDOUT_FILENO) != 0; }

static void md_esc(const char *code) {
  if (out_color())
    fputs(code, stdout);
}

static void write_text(const char *text, MD_SIZE size) {
  if (size > 0)
    fwrite(text, 1, size, stdout);
}

static void attr_copy(char *dst, size_t cap, const MD_ATTRIBUTE *attr) {
  if (!dst || cap == 0)
    return;
  dst[0] = '\0';
  if (!attr || !attr->text || attr->size == 0)
    return;
  size_t n = attr->size < cap - 1 ? attr->size : cap - 1;
  memcpy(dst, attr->text, n);
  dst[n] = '\0';
}

static bool utf8_decode_one(const char *s, uint32_t *cp, int *bytes) {
  const unsigned char *p = (const unsigned char *)s;
  if (p[0] < 0x80) {
    *cp = p[0];
    *bytes = 1;
    return true;
  }
  if ((p[0] & 0xE0) == 0xC0 && p[1] && (p[1] & 0xC0) == 0x80) {
    *cp = ((uint32_t)(p[0] & 0x1F) << 6) | (uint32_t)(p[1] & 0x3F);
    *bytes = 2;
    return true;
  }
  if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2] &&
      (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
    *cp = ((uint32_t)(p[0] & 0x0F) << 12) |
          ((uint32_t)(p[1] & 0x3F) << 6) | (uint32_t)(p[2] & 0x3F);
    *bytes = 3;
    return true;
  }
  if ((p[0] & 0xF8) == 0xF0 && p[1] && p[2] && p[3] &&
      (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 &&
      (p[3] & 0xC0) == 0x80) {
    *cp = ((uint32_t)(p[0] & 0x07) << 18) |
          ((uint32_t)(p[1] & 0x3F) << 12) |
          ((uint32_t)(p[2] & 0x3F) << 6) | (uint32_t)(p[3] & 0x3F);
    *bytes = 4;
    return true;
  }
  *cp = p[0];
  *bytes = 1;
  return false;
}

static int codepoint_width(uint32_t cp) {
  if (cp == 0 || cp < 32 || (cp >= 0x7f && cp < 0xa0))
    return 0;
  if ((cp >= 0x0300 && cp <= 0x036f) || (cp >= 0xfe00 && cp <= 0xfe0f) ||
      cp == 0x200d)
    return 0;
  if ((cp >= 0x1100 && cp <= 0x115f) || cp == 0x2329 || cp == 0x232a ||
      (cp >= 0x2e80 && cp <= 0xa4cf) || (cp >= 0xac00 && cp <= 0xd7a3) ||
      (cp >= 0xf900 && cp <= 0xfaff) || (cp >= 0xfe10 && cp <= 0xfe19) ||
      (cp >= 0xfe30 && cp <= 0xfe6f) || (cp >= 0xff00 && cp <= 0xff60) ||
      (cp >= 0x2600 && cp <= 0x27bf) ||
      (cp >= 0x1f300 && cp <= 0x1faff))
    return 2;
  return 1;
}

static int display_width(const char *s) {
  int width = 0;
  for (size_t off = 0; s && s[off];) {
    uint32_t cp;
    int n;
    utf8_decode_one(s + off, &cp, &n);
    width += codepoint_width(cp);
    off += (size_t)n;
  }
  return width;
}

static void pad_display(const char *s, int width) {
  int pad = width - display_width(s);
  while (pad-- > 0)
    putchar(' ');
}

static void table_append(MdRender *r, const char *text, MD_SIZE size) {
  if (r->table_row < 0 || r->table_row >= MD_TABLE_MAX_ROWS ||
      r->table_cell < 0 || r->table_cell >= MD_TABLE_MAX_COLS)
    return;
  char *cell = r->table_cells[r->table_row][r->table_cell];
  size_t len = strlen(cell);
  for (MD_SIZE i = 0; i < size && len + 1 < MD_TABLE_CELL_MAX; i++) {
    cell[len++] = (text[i] == '\n' || text[i] == '\r' || text[i] == '\t') ? ' ' : text[i];
  }
  cell[len] = '\0';
}

static void render_table(MdRender *r) {
  int rows = r->table_row;
  int cols = r->table_cols;
  if (rows <= 0 || cols <= 0)
    return;

  int widths[MD_TABLE_MAX_COLS] = {0};
  for (int row = 0; row < rows; row++) {
    for (int col = 0; col < cols; col++) {
      int w = display_width(r->table_cells[row][col]);
      if (w > widths[col])
        widths[col] = w;
    }
  }
  for (int col = 0; col < cols; col++)
    if (widths[col] < 3)
      widths[col] = 3;

  start_line(r);
  md_esc(ESC_GRAY);
  fputs("┌", stdout);
  for (int col = 0; col < cols; col++) {
    for (int i = 0; i < widths[col] + 2; i++)
      fputs("─", stdout);
    fputs(col == cols - 1 ? "┐" : "┬", stdout);
  }
  md_esc(ESC_RESET);
  newline(r);

  for (int row = 0; row < rows; row++) {
    start_line(r);
    md_esc(ESC_GRAY);
    fputs("│", stdout);
    md_esc(ESC_RESET);
    for (int col = 0; col < cols; col++) {
      putchar(' ');
      if (r->table_header[row])
        md_esc(ESC_BOLD);
      fputs(r->table_cells[row][col], stdout);
      md_esc(ESC_RESET);
      pad_display(r->table_cells[row][col], widths[col]);
      putchar(' ');
      md_esc(ESC_GRAY);
      fputs("│", stdout);
      md_esc(ESC_RESET);
    }
    newline(r);

    if (r->table_header[row]) {
      start_line(r);
      md_esc(ESC_GRAY);
      fputs("├", stdout);
      for (int col = 0; col < cols; col++) {
        for (int i = 0; i < widths[col] + 2; i++)
          fputs("─", stdout);
        fputs(col == cols - 1 ? "┤" : "┼", stdout);
      }
      md_esc(ESC_RESET);
      newline(r);
    }
  }

  start_line(r);
  md_esc(ESC_GRAY);
  fputs("└", stdout);
  for (int col = 0; col < cols; col++) {
    for (int i = 0; i < widths[col] + 2; i++)
      fputs("─", stdout);
    fputs(col == cols - 1 ? "┘" : "┴", stdout);
  }
  md_esc(ESC_RESET);
  newline(r);
}

static void newline(MdRender *r) {
  putchar('\n');
  r->at_line_start = true;
}

static void ensure_line_start(MdRender *r) {
  if (!r->at_line_start)
    newline(r);
}

static void blank_line(MdRender *r) {
  ensure_line_start(r);
  putchar('\n');
  r->at_line_start = true;
}

static void maybe_block_gap(MdRender *r) {
  if (r->need_block_gap) {
    blank_line(r);
    r->need_block_gap = false;
  }
}

static void print_indent(MdRender *r, int list_indent) {
  for (int i = 0; i < r->quote_depth; i++) {
    md_esc(ESC_GRAY);
    fputs("│ ", stdout);
    md_esc(ESC_RESET);
  }
  for (int i = 0; i < list_indent; i++)
    fputs("  ", stdout);
}

static void start_line(MdRender *r) {
  if (!r->at_line_start)
    return;
  int list_indent = r->list_depth;
  if (r->pending_li_marker && list_indent > 0)
    list_indent--;
  print_indent(r, list_indent);
  if (r->pending_li_marker) {
    int depth = r->list_depth > 0 ? r->list_depth - 1 : 0;
    int ordered = depth < 16 ? r->ordered_stack[depth] : 0;
    if (ordered) {
      int n = depth < 16 ? r->item_stack[depth]++ : 1;
      printf("%d. ", n);
    } else {
      md_esc(ESC_CYAN);
      fputs("- ", stdout);
      md_esc(ESC_RESET);
    }
    r->pending_li_marker = false;
  }
  r->at_line_start = false;
}

static void print_render_text(MdRender *r, const char *text, MD_SIZE size) {
  if (r->link_mode == LINK_IMAGE_ALT) {
    size_t room = sizeof(r->image_alt) - r->image_alt_len - 1;
    size_t n = size < room ? size : room;
    if (n > 0) {
      memcpy(r->image_alt + r->image_alt_len, text, n);
      r->image_alt_len += n;
      r->image_alt[r->image_alt_len] = '\0';
    }
    return;
  }

  if (r->in_code_block) {
    md_esc(ESC_GRAY);
    for (MD_SIZE i = 0; i < size; i++) {
      if (!r->code_line_open) {
        start_line(r);
        fputs("│ ", stdout);
        r->code_line_open = true;
      }
      putchar(text[i]);
      if (text[i] == '\n') {
        r->at_line_start = true;
        r->code_line_open = false;
      } else {
        r->at_line_start = false;
      }
    }
    md_esc(ESC_RESET);
    return;
  }

  if (r->in_table && r->in_table_cell) {
    table_append(r, text, size);
    return;
  }

  start_line(r);
  write_text(text, size);
  if (size > 0)
    r->at_line_start = text[size - 1] == '\n';
}

static int enter_block(MD_BLOCKTYPE type, void *detail, void *userdata) {
  MdRender *r = userdata;
  switch (type) {
  case MD_BLOCK_DOC:
    break;
  case MD_BLOCK_QUOTE:
    maybe_block_gap(r);
    r->quote_depth++;
    break;
  case MD_BLOCK_UL:
    maybe_block_gap(r);
    if (r->list_depth < 16) {
      r->ordered_stack[r->list_depth] = 0;
      r->item_stack[r->list_depth] = 1;
    }
    r->list_depth++;
    break;
  case MD_BLOCK_OL: {
    MD_BLOCK_OL_DETAIL *d = detail;
    maybe_block_gap(r);
    if (r->list_depth < 16) {
      r->ordered_stack[r->list_depth] = 1;
      r->item_stack[r->list_depth] = d && d->start > 0 ? (int)d->start : 1;
    }
    r->list_depth++;
    break;
  }
  case MD_BLOCK_LI:
    ensure_line_start(r);
    r->pending_li_marker = true;
    break;
  case MD_BLOCK_H: {
    MD_BLOCK_H_DETAIL *d = detail;
    maybe_block_gap(r);
    ensure_line_start(r);
    r->heading_level = d ? (int)d->level : 1;
    md_esc(ESC_BOLD);
    md_esc(r->heading_level <= 2 ? ESC_CYAN : ESC_GREEN);
    r->at_line_start = false;
    break;
  }
  case MD_BLOCK_HR:
    maybe_block_gap(r);
    ensure_line_start(r);
    md_esc(ESC_GRAY);
    fputs("────────────────────────────────────────", stdout);
    md_esc(ESC_RESET);
    newline(r);
    r->need_block_gap = true;
    break;
  case MD_BLOCK_CODE: {
    MD_BLOCK_CODE_DETAIL *d = detail;
    attr_copy(r->code_lang, sizeof(r->code_lang), d ? &d->lang : NULL);
    maybe_block_gap(r);
    ensure_line_start(r);
    start_line(r);
    md_esc(ESC_GRAY);
    fputs("┌", stdout);
    if (r->code_lang[0]) {
      printf(" %s ", r->code_lang);
      int label_width = display_width(r->code_lang) + 2;
      for (int i = label_width; i < 42; i++)
        fputs("─", stdout);
    } else {
      for (int i = 0; i < 42; i++)
        fputs("─", stdout);
    }
    fputs("┐", stdout);
    md_esc(ESC_RESET);
    newline(r);
    r->in_code_block = true;
    r->code_line_open = false;
    break;
  }
  case MD_BLOCK_P:
    maybe_block_gap(r);
    r->paragraph_open = true;
    break;
  case MD_BLOCK_TABLE:
    maybe_block_gap(r);
    memset(r->table_cells, 0, sizeof(r->table_cells));
    memset(r->table_header, 0, sizeof(r->table_header));
    r->in_table = true;
    r->table_row = 0;
    r->table_cell = 0;
    r->table_cols = 0;
    r->table_header_rows = 0;
    break;
  case MD_BLOCK_THEAD:
    r->in_table_head = true;
    break;
  case MD_BLOCK_TBODY:
    break;
  case MD_BLOCK_TR:
    r->table_cell = 0;
    r->table_row_is_header = r->in_table_head;
    break;
  case MD_BLOCK_TH:
  case MD_BLOCK_TD:
    r->in_table_cell = true;
    break;
  case MD_BLOCK_HTML:
  case MD_BLOCK_FOOTNOTE_DEF_SECTION:
  case MD_BLOCK_FOOTNOTE_DEF:
  case MD_BLOCK_ADMONITION:
    maybe_block_gap(r);
    break;
  }
  return 0;
}

static int leave_block(MD_BLOCKTYPE type, void *detail, void *userdata) {
  (void)detail;
  MdRender *r = userdata;
  switch (type) {
  case MD_BLOCK_DOC:
    if (!r->at_line_start)
      newline(r);
    break;
  case MD_BLOCK_QUOTE:
    if (r->quote_depth > 0)
      r->quote_depth--;
    r->need_block_gap = true;
    break;
  case MD_BLOCK_UL:
  case MD_BLOCK_OL:
    if (r->list_depth > 0)
      r->list_depth--;
    r->need_block_gap = r->list_depth == 0;
    break;
  case MD_BLOCK_LI:
    if (!r->at_line_start)
      newline(r);
    r->pending_li_marker = false;
    break;
  case MD_BLOCK_H:
    md_esc(ESC_RESET);
    newline(r);
    r->heading_level = 0;
    r->need_block_gap = true;
    break;
  case MD_BLOCK_CODE:
    if (!r->at_line_start)
      newline(r);
    r->in_code_block = false;
    r->code_line_open = false;
    start_line(r);
    md_esc(ESC_GRAY);
    fputs("└", stdout);
    for (int i = 0; i < 42; i++)
      fputs("─", stdout);
    fputs("┘", stdout);
    md_esc(ESC_RESET);
    newline(r);
    r->code_lang[0] = '\0';
    r->need_block_gap = true;
    break;
  case MD_BLOCK_P:
    if (!r->at_line_start)
      newline(r);
    r->paragraph_open = false;
    r->need_block_gap = r->list_depth == 0 && r->quote_depth == 0;
    break;
  case MD_BLOCK_TABLE:
    render_table(r);
    r->in_table = false;
    r->in_table_head = false;
    r->in_table_cell = false;
    r->need_block_gap = true;
    break;
  case MD_BLOCK_TR:
    if (r->table_row < MD_TABLE_MAX_ROWS) {
      r->table_header[r->table_row] = r->table_row_is_header;
      if (r->table_cell > r->table_cols)
        r->table_cols = r->table_cell;
      if (r->table_row_is_header)
        r->table_header_rows++;
      r->table_row++;
    }
    break;
  case MD_BLOCK_TH:
  case MD_BLOCK_TD:
    r->in_table_cell = false;
    if (r->table_cell < MD_TABLE_MAX_COLS)
      r->table_cell++;
    break;
  case MD_BLOCK_THEAD:
    r->in_table_head = false;
    break;
  case MD_BLOCK_TBODY:
  case MD_BLOCK_HR:
  case MD_BLOCK_HTML:
  case MD_BLOCK_FOOTNOTE_DEF_SECTION:
  case MD_BLOCK_FOOTNOTE_DEF:
  case MD_BLOCK_ADMONITION:
    break;
  }
  return 0;
}

static int enter_span(MD_SPANTYPE type, void *detail, void *userdata) {
  MdRender *r = userdata;
  if (r->in_table && r->in_table_cell) {
    switch (type) {
    case MD_SPAN_CODE:
      table_append(r, "`", 1);
      break;
    case MD_SPAN_A: {
      MD_SPAN_A_DETAIL *d = detail;
      attr_copy(r->link_href, sizeof(r->link_href), d ? &d->href : NULL);
      r->link_mode = LINK_TEXT;
      break;
    }
    case MD_SPAN_IMG: {
      MD_SPAN_IMG_DETAIL *d = detail;
      attr_copy(r->image_src, sizeof(r->image_src), d ? &d->src : NULL);
      r->image_alt[0] = '\0';
      r->image_alt_len = 0;
      r->link_mode = LINK_IMAGE_ALT;
      break;
    }
    case MD_SPAN_EM:
    case MD_SPAN_STRONG:
    case MD_SPAN_DEL:
    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
    case MD_SPAN_WIKILINK:
    case MD_SPAN_U:
    case MD_SPAN_SPOILER:
    case MD_SPAN_SUPERSCRIPT:
    case MD_SPAN_SUBSCRIPT:
    case MD_SPAN_FOOTNOTE_REF:
      break;
    }
    return 0;
  }

  switch (type) {
  case MD_SPAN_STRONG:
    md_esc(ESC_BOLD);
    break;
  case MD_SPAN_EM:
    md_esc(ESC_DIM);
    break;
  case MD_SPAN_CODE:
    r->span_code = true;
    md_esc(ESC_CYAN);
    break;
  case MD_SPAN_DEL:
    r->span_del = true;
    md_esc(ESC_DIM);
    break;
  case MD_SPAN_A: {
    MD_SPAN_A_DETAIL *d = detail;
    attr_copy(r->link_href, sizeof(r->link_href), d ? &d->href : NULL);
    r->link_mode = LINK_TEXT;
    md_esc(ESC_CYAN);
    break;
  }
  case MD_SPAN_IMG: {
    MD_SPAN_IMG_DETAIL *d = detail;
    attr_copy(r->image_src, sizeof(r->image_src), d ? &d->src : NULL);
    r->image_alt[0] = '\0';
    r->image_alt_len = 0;
    r->link_mode = LINK_IMAGE_ALT;
    break;
  }
  case MD_SPAN_LATEXMATH:
  case MD_SPAN_LATEXMATH_DISPLAY:
  case MD_SPAN_WIKILINK:
  case MD_SPAN_U:
  case MD_SPAN_SPOILER:
  case MD_SPAN_SUPERSCRIPT:
  case MD_SPAN_SUBSCRIPT:
  case MD_SPAN_FOOTNOTE_REF:
    break;
  }
  return 0;
}

static int leave_span(MD_SPANTYPE type, void *detail, void *userdata) {
  (void)detail;
  MdRender *r = userdata;
  if (r->in_table && r->in_table_cell) {
    switch (type) {
    case MD_SPAN_CODE:
      table_append(r, "`", 1);
      break;
    case MD_SPAN_A:
      if (r->link_href[0]) {
        table_append(r, " (", 2);
        table_append(r, r->link_href, (MD_SIZE)strlen(r->link_href));
        table_append(r, ")", 1);
      }
      r->link_href[0] = '\0';
      r->link_mode = LINK_NONE;
      break;
    case MD_SPAN_IMG:
      if (r->image_alt[0]) {
        table_append(r, "[image: ", 8);
        table_append(r, r->image_alt, (MD_SIZE)strlen(r->image_alt));
        table_append(r, "]", 1);
      } else {
        table_append(r, "[image]", 7);
      }
      if (r->image_src[0]) {
        table_append(r, " (", 2);
        table_append(r, r->image_src, (MD_SIZE)strlen(r->image_src));
        table_append(r, ")", 1);
      }
      r->image_src[0] = '\0';
      r->image_alt[0] = '\0';
      r->image_alt_len = 0;
      r->link_mode = LINK_NONE;
      break;
    case MD_SPAN_EM:
    case MD_SPAN_STRONG:
    case MD_SPAN_DEL:
    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
    case MD_SPAN_WIKILINK:
    case MD_SPAN_U:
    case MD_SPAN_SPOILER:
    case MD_SPAN_SUPERSCRIPT:
    case MD_SPAN_SUBSCRIPT:
    case MD_SPAN_FOOTNOTE_REF:
      break;
    }
    return 0;
  }

  switch (type) {
  case MD_SPAN_STRONG:
  case MD_SPAN_EM:
    md_esc(ESC_RESET);
    break;
  case MD_SPAN_CODE:
    md_esc(ESC_RESET);
    r->span_code = false;
    break;
  case MD_SPAN_DEL:
    md_esc(ESC_RESET);
    r->span_del = false;
    break;
  case MD_SPAN_A:
    md_esc(ESC_RESET);
    if (r->link_href[0]) {
      md_esc(ESC_GRAY);
      printf(" (%s)", r->link_href);
      md_esc(ESC_RESET);
    }
    r->link_href[0] = '\0';
    r->link_mode = LINK_NONE;
    break;
  case MD_SPAN_IMG:
    start_line(r);
    md_esc(ESC_GRAY);
    if (r->image_alt[0])
      printf("[image: %s]", r->image_alt);
    else
      fputs("[image]", stdout);
    if (r->image_src[0])
      printf(" (%s)", r->image_src);
    md_esc(ESC_RESET);
    r->image_src[0] = '\0';
    r->image_alt[0] = '\0';
    r->image_alt_len = 0;
    r->link_mode = LINK_NONE;
    break;
  case MD_SPAN_LATEXMATH:
  case MD_SPAN_LATEXMATH_DISPLAY:
  case MD_SPAN_WIKILINK:
  case MD_SPAN_U:
  case MD_SPAN_SPOILER:
  case MD_SPAN_SUPERSCRIPT:
  case MD_SPAN_SUBSCRIPT:
  case MD_SPAN_FOOTNOTE_REF:
    break;
  }
  return 0;
}

static int text_cb(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size,
                   void *userdata) {
  MdRender *r = userdata;
  switch (type) {
  case MD_TEXT_BR:
  case MD_TEXT_SOFTBR:
    if (r->in_table && r->in_table_cell)
      table_append(r, " ", 1);
    else
      newline(r);
    break;
  case MD_TEXT_NULLCHAR:
    break;
  case MD_TEXT_ENTITY:
  case MD_TEXT_NORMAL:
  case MD_TEXT_CODE:
  case MD_TEXT_HTML:
  case MD_TEXT_LATEXMATH:
    print_render_text(r, text, size);
    break;
  }
  return 0;
}

void ui_print_markdown(const char *text) {
  if (!text) {
    putchar('\n');
    return;
  }

  MdRender render = {
      .at_line_start = true,
  };
  MD_PARSER parser = {
      .abi_version = 0,
      .flags = MD_DIALECT_GITHUB | MD_FLAG_NOHTML,
      .enter_block = enter_block,
      .leave_block = leave_block,
      .enter_span = enter_span,
      .leave_span = leave_span,
      .text = text_cb,
      .debug_log = NULL,
      .syntax = NULL,
  };

  if (md_parse(text, (MD_SIZE)strlen(text), &parser, &render) != 0) {
    fputs(text, stdout);
    if (text[0] && text[strlen(text) - 1] != '\n')
      putchar('\n');
  }
}
