#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <glib.h>
#include <quickjs-libc.h>
#include <quickjs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include <yoga/Yoga.h>

typedef struct {
  JSContext *ctx;
  JSValue func;
  uv_timer_t *timer;
  int is_interval;
} TimerData;

// 新增：统一资源释放回调
static void timer_close_cb(uv_handle_t *handle) {
  TimerData *td = (TimerData *)handle->data;
  JS_FreeValue(td->ctx, td->func);
  free(td);
  free(handle);
}

// ---------------------- 遍历关闭所有定时器 ----------------------
void close_timers_cb(uv_handle_t *handle, void *arg) {
  if (uv_handle_get_type(handle) == UV_TIMER && !uv_is_closing(handle)) {
    uv_timer_stop((uv_timer_t *)handle);
    uv_close(handle, timer_close_cb);
  }
}

static void timer_cb(uv_timer_t *handle) {
  TimerData *td = (TimerData *)handle->data;
  JSContext *ctx = td->ctx;

  JSValue ret = JS_Call(ctx, td->func, JS_UNDEFINED, 0, NULL);
  if (JS_IsException(ret)) {
    js_std_dump_error(ctx);
  }
  JS_FreeValue(ctx, ret);

  if (!td->is_interval) {
    uv_timer_stop(handle);
    uv_close((uv_handle_t *)handle, timer_close_cb);
  }
}

static JSValue js_setTimeout(JSContext *ctx, JSValue this_val, int argc,
                             JSValue *argv) {
  int64_t delay;
  if (JS_ToInt64(ctx, &delay, argv[1]) != 0) {
    return JS_EXCEPTION;
  }

  JSValue func = JS_DupValue(ctx, argv[0]);

  uv_timer_t *timer = malloc(sizeof(uv_timer_t));
  TimerData *td = malloc(sizeof(TimerData));
  td->ctx = ctx;
  td->func = func;
  td->timer = timer;
  td->is_interval = 0;

  uv_timer_init(uv_default_loop(), timer);
  timer->data = td;
  uv_timer_start(timer, timer_cb, delay, 0);

  return JS_NewInt64(ctx, (int64_t)timer);
}

static JSValue js_setInterval(JSContext *ctx, JSValue this_val, int argc,
                              JSValue *argv) {
  int64_t interval;
  if (JS_ToInt64(ctx, &interval, argv[1]) != 0) {
    return JS_EXCEPTION;
  }

  JSValue func = JS_DupValue(ctx, argv[0]);

  uv_timer_t *timer = malloc(sizeof(uv_timer_t));
  TimerData *td = malloc(sizeof(TimerData));
  td->ctx = ctx;
  td->func = func;
  td->timer = timer;
  td->is_interval = 1;

  uv_timer_init(uv_default_loop(), timer);
  timer->data = td;
  uv_timer_start(timer, timer_cb, interval, interval);

  return JS_NewInt64(ctx, (int64_t)timer);
}

static JSValue js_clearTimer(JSContext *ctx, JSValue this_val, int argc,
                             JSValue *argv) {
  int64_t timer_ptr;
  if (JS_ToInt64(ctx, &timer_ptr, argv[0]) != 0) {
    return JS_EXCEPTION;
  }

  uv_timer_t *timer = (uv_timer_t *)timer_ptr;
  uv_timer_stop(timer);
  uv_close((uv_handle_t *)timer, timer_close_cb); // 使用统一回调

  return JS_UNDEFINED;
}

int readfile(const char *filename, char **out) {
  FILE *f = fopen(filename, "rb");
  if (!f)
    return -1;

  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  *out = malloc(fsize + 1);
  fread(*out, fsize, 1, f);
  fclose(f);
  (*out)[fsize] = 0;
  return fsize;
}

// 清理函数封装
void cleanup_resources(JSRuntime *rt, JSContext *ctx, uv_loop_t *loop,
                       char *code, JSValue val) {
  // 安全判断 JSValue 类型
  if (!JS_IsUndefined(val)) {
    JS_FreeValue(ctx, val);
  }

  // 释放文件内容内存
  if (code != NULL) {
    free(code);
  }

  // 清理 QuickJS 运行时
  if (rt != NULL) {
    js_std_free_handlers(rt);
    if (ctx != NULL) {
      JS_FreeContext(ctx);
    }
    JS_FreeRuntime(rt);
  }

  // 关闭 libuv 事件循环
  if (loop != NULL) {
    uv_loop_close(loop);
  }
}

/*-------------------------------------
 * 颜色结构体定义
 *-----------------------------------*/
typedef struct {
  Uint8 r, g, b, a;
} Color;

// 预设颜色
static const Color COLOR_TRANSPARENT = {255, 255, 255, 0};
static const Color COLOR_WHITE = {255, 255, 255, 255};
static const Color COLOR_BLACK = {0, 0, 0, 255};
static const Color COLOR_RED = {255, 0, 0, 255};
static const Color COLOR_HIGHLIGHT = {255, 255, 0, 255}; // 选中高亮色

/*-------------------------------------
 * 样式结构体定义
 *-----------------------------------*/
typedef struct NodeStyle {
  // 布局属性
  float flex;
  float margin;
  YGFlexDirection flexDirection;
  YGJustify justifyContent;

  // 渲染属性
  Color backgroundColor;
  Color borderColor;
} NodeStyle;

// 定义事件监听器结构体
typedef struct EventListener {
  char *event_type; // 事件类型
  JSValue callback; // 回调函数
  struct EventListener *next;
} EventListener;

typedef enum {
  NODE, // 普通节点
  TEXT  // 文字节点
} NodeType;

/*-------------------------------------
 * 树节点结构体定义
 *-----------------------------------*/
typedef struct TreeNode {
  int id;
  NodeType node_type;
  char *text;
  // int js_refcount; // 新增：JavaScript引用计数
  NodeStyle *style;
  int childCount;
  struct TreeNode **children;
  struct TreeNode *parent;
  YGNodeRef yogaNode;
  EventListener *event_listeners; // 存储事件监听器
} TreeNode;

/*-------------------------------------
 * 全局状态
 *-----------------------------------*/
GHashTable *nodeIdMap = NULL;
TreeNode *selectedNode = NULL;
YGNodeRef yogaRoot = NULL;
TreeNode *root_data = NULL;
int nextNodeId = 0;
int VIEW_WIDTH = 1000;
int VIEW_HEIGHT = 600;

/*-------------------------------------
 * 核心功能实现
 *-----------------------------------*/
Color parse_color(const char *hex) {
  Color color = COLOR_BLACK;
  if (hex && hex[0] == '#') {
    unsigned int rgb = 0;
    sscanf(hex + 1, "%x", &rgb);
    if (strlen(hex + 1) == 6) {
      color.r = (rgb >> 16) & 0xFF;
      color.g = (rgb >> 8) & 0xFF;
      color.b = rgb & 0xFF;
    } else if (strlen(hex + 1) == 3) {
      color.r = ((rgb >> 8) & 0xF) * 17;
      color.g = ((rgb >> 4) & 0xF) * 17;
      color.b = (rgb & 0xF) * 17;
    }
  }
  return color;
}

YGNodeRef create_yoga_node(TreeNode *data) {
  YGNodeRef yogaNode = YGNodeNew();
  YGNodeStyleSetFlex(yogaNode, data->style->flex);
  YGNodeStyleSetMargin(yogaNode, YGEdgeAll, data->style->margin);
  YGNodeStyleSetFlexDirection(yogaNode, data->style->flexDirection);
  YGNodeStyleSetJustifyContent(yogaNode, data->style->justifyContent);
  return yogaNode;
}

TreeNode *create_node(NodeType node_type, const char *text, float flex,
                      float margin, YGFlexDirection flexDirection,
                      YGJustify justifyContent) {
  TreeNode *node = (TreeNode *)malloc(sizeof(TreeNode));
  node->id = ++nextNodeId;
  g_hash_table_insert(nodeIdMap, &node->id, node);

  node->node_type = node_type;
  if (node_type == TEXT) {
    node->text = strdup(text); // 复制文字内容
  } else {
    node->text = NULL;
  }

  node->style = (NodeStyle *)malloc(sizeof(NodeStyle));
  node->style->flex = flex;
  node->style->margin = margin;
  node->style->flexDirection = flexDirection;
  node->style->justifyContent = justifyContent;
  // 初始化白底黑边
  node->style->backgroundColor = COLOR_WHITE;
  node->style->borderColor = COLOR_BLACK;
  node->childCount = 0;
  node->children = NULL;
  node->parent = NULL;
  node->event_listeners = NULL;

  node->yogaNode = create_yoga_node(node);

  return node;
}

void set_node_text(TreeNode *node, const char *text) {
  if (!node || node->node_type != TEXT) {
    return;
  }
  free(node->text);          // 释放旧的文字内容
  node->text = strdup(text); // 复制新的文字内容
}

void free_tree(JSContext *ctx, TreeNode *node) {
  if (node) {
    if (node->node_type == TEXT) {
      free(node->text);
    }
    EventListener *listener = node->event_listeners;
    while (listener) {
      EventListener *next = listener->next;
      JS_FreeValue(ctx, listener->callback);
      free(listener->event_type);
      free(listener);
      listener = next;
    }
    node->event_listeners = NULL;
    for (int i = 0; i < node->childCount; i++) {
      free_tree(ctx, node->children[i]);
    }
    YGNodeFree(node->yogaNode);
    free(node->children);
    g_hash_table_remove(nodeIdMap, &node->id);
    free(node->style);
    free(node);
  }
}

void add_listener(JSContext *ctx, TreeNode *node, const char *event_type,
                  JSValue callback) {
  if (!node || !event_type || JS_IsNull(callback) || JS_IsUndefined(callback)) {
    return;
  }
  // printf("step into add_listener %s id: %d\n", event_type, node->id);
  // 分配内存并初始化新的事件监听器
  EventListener *new_listener = malloc(sizeof(EventListener));
  new_listener->event_type = strdup(event_type);
  new_listener->callback = JS_DupValue(ctx, callback);
  new_listener->next = NULL;
  // 将新监听器添加到链表头部
  EventListener *current = node->event_listeners;
  if (current == NULL) {
    node->event_listeners = new_listener;
  } else {
    new_listener->next = current;
    node->event_listeners = new_listener;
  }
}

void remove_listener(JSContext *ctx, TreeNode *node, const char *event_type,
                     JSValue callback) {
  if (!node || !event_type || JS_IsNull(callback) || JS_IsUndefined(callback)) {
    return;
  }

  // printf("step into remove_listener %s id: %d\n", event_type, node->id);

  EventListener **prev_ptr = &node->event_listeners;
  EventListener *current = node->event_listeners;

  while (current) {
    if (strcmp(current->event_type, event_type) == 0 &&
        JS_VALUE_GET_OBJ(current->callback) == JS_VALUE_GET_OBJ(callback)) {
      // 移除当前节点
      *prev_ptr = current->next;
      JS_FreeValue(ctx, current->callback); // 使用传入的 ctx
      free(current->event_type);
      free(current);
      break;
    }
    prev_ptr = &current->next;
    current = current->next;
  }
}

static JSClassID tree_node_class_id;

JSValue wrap_node(JSContext *ctx, TreeNode *node) {
  // 创建对象并关联类
  JSValue obj = JS_NewObjectClass(ctx, tree_node_class_id);
  if (JS_IsException(obj))
    return obj;

  // 绑定 C 指针到 JS 对象
  JS_SetOpaque(obj, node);
  // node->js_refcount++; // 增加引用计数
  return obj;
}

void dispatch_event(JSContext *ctx, TreeNode *node, const char *event_type) {
  if (!node || !event_type)
    return;

  // 创建合成事件对象（仅包含必要字段）
  JSValue event_obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, event_obj, "type", JS_NewString(ctx, event_type));
  JS_SetPropertyStr(ctx, event_obj, "target",
                    wrap_node(ctx, node)); // wrap_node 需提前实现

  // 遍历当前节点的监听器
  EventListener *listener = node->event_listeners;
  while (listener) {
    if (strcmp(listener->event_type, event_type) == 0) {
      // 调用 JS 回调函数
      JSValue ret =
          JS_Call(ctx, listener->callback, JS_UNDEFINED, 1, &event_obj);
      if (JS_IsException(ret)) {
        // 处理异常（例如打印错误）
        js_std_dump_error(ctx);
      }
      JS_FreeValue(ctx, ret);
    }
    listener = listener->next;
  }

  // 释放事件对象
  JS_FreeValue(ctx, event_obj);
}

// 解包 JS 对象为 TreeNode*
TreeNode *unwrap_node(JSContext *ctx, JSValueConst val) {
  if (!JS_IsObject(val)) {
    JS_ThrowTypeError(ctx, "Expected TreeNode object");
    return NULL;
  }
  return JS_GetOpaque(val, tree_node_class_id);
}

int append_child(TreeNode *parent, TreeNode *child) {
  if (!parent || !child || child->parent)
    return 0;
  parent->children =
      realloc(parent->children, sizeof(TreeNode *) * (parent->childCount + 1));
  parent->children[parent->childCount++] = child;
  child->parent = parent;
  YGNodeInsertChild(parent->yogaNode, child->yogaNode, parent->childCount - 1);
  return 1;
}

int insert_before(TreeNode *parent, TreeNode *newChild, TreeNode *refChild) {
  if (!parent || !newChild || !refChild || parent != refChild->parent)
    return 0;

  int index = -1;
  for (int i = 0; i < parent->childCount; i++) {
    if (parent->children[i] == refChild) {
      index = i;
      break;
    }
  }
  if (index == -1)
    return 0;

  parent->children =
      realloc(parent->children, sizeof(TreeNode *) * (parent->childCount + 1));
  memmove(&parent->children[index + 1], &parent->children[index],
          sizeof(TreeNode *) * (parent->childCount - index));
  parent->children[index] = newChild;
  parent->childCount++;
  newChild->parent = parent;
  YGNodeInsertChild(parent->yogaNode, newChild->yogaNode, index);
  return 1;
}

int remove_child(JSContext *ctx, TreeNode *parent, TreeNode *child) {
  if (!parent || !child || parent != child->parent)
    return 0;

  int index = -1;
  for (int i = 0; i < parent->childCount; i++) {
    if (parent->children[i] == child) {
      index = i;
      break;
    }
  }
  if (index == -1)
    return 0;

  YGNodeRemoveChild(parent->yogaNode, child->yogaNode);
  memmove(&parent->children[index], &parent->children[index + 1],
          sizeof(TreeNode *) * (parent->childCount - index - 1));
  parent->childCount--;
  parent->children =
      realloc(parent->children, sizeof(TreeNode *) * parent->childCount);
  free_tree(ctx, child);
  return 1;
}

/*-------------------------------------
 * 新增：样式属性设置函数
 * 参数说明：
 * - node: 目标节点
 * - attr: 属性名（字符串）
 * - value: 属性值（字符串形式）
 * 返回值：1成功 0失败
 *-----------------------------------*/
int set_attribute(TreeNode *node, const char *attr, const char *value) {
  if (!node || !attr || !value)
    return 0;

  // 布局属性处理
  if (strcmp(attr, "flex") == 0) {
    float flex = atof(value);
    node->style->flex = flex;
    YGNodeStyleSetFlex(node->yogaNode, flex);
    return 1;
  } else if (strcmp(attr, "margin") == 0) {
    float margin = atof(value);
    node->style->margin = margin;
    YGNodeStyleSetMargin(node->yogaNode, YGEdgeAll, margin);
    return 1;
  } else if (strcmp(attr, "flexDirection") == 0) {
    if (strcmp(value, "row") == 0) {
      node->style->flexDirection = YGFlexDirectionRow;
      YGNodeStyleSetFlexDirection(node->yogaNode, YGFlexDirectionRow);
    } else if (strcmp(value, "column") == 0) {
      node->style->flexDirection = YGFlexDirectionColumn;
      YGNodeStyleSetFlexDirection(node->yogaNode, YGFlexDirectionColumn);
    } else {
      return 0;
    }
    return 1;
  } else if (strcmp(attr, "justifyContent") == 0) {
    if (strcmp(value, "flex-start") == 0) {
      node->style->justifyContent = YGJustifyFlexStart;
      YGNodeStyleSetJustifyContent(node->yogaNode, YGJustifyFlexStart);
    } else if (strcmp(value, "center") == 0) {
      node->style->justifyContent = YGJustifyCenter;
      YGNodeStyleSetJustifyContent(node->yogaNode, YGJustifyCenter);
    } else if (strcmp(value, "flex-end") == 0) {
      node->style->justifyContent = YGJustifyFlexEnd;
      YGNodeStyleSetJustifyContent(node->yogaNode, YGJustifyFlexEnd);
    } else if (strcmp(value, "space-between") == 0) {
      node->style->justifyContent = YGJustifySpaceBetween;
      YGNodeStyleSetJustifyContent(node->yogaNode, YGJustifySpaceBetween);
    } else if (strcmp(value, "space-around") == 0) {
      node->style->justifyContent = YGJustifySpaceAround;
      YGNodeStyleSetJustifyContent(node->yogaNode, YGJustifySpaceAround);
    } else {
      return 0;
    }
    return 1;
  }

  // 渲染属性处理
  else if (strcmp(attr, "backgroundColor") == 0) {
    node->style->backgroundColor = parse_color(value);
    return 1;
  } else if (strcmp(attr, "borderColor") == 0) {
    node->style->borderColor = parse_color(value);
    return 1;
  }

  return 0; // 未知属性
}

void update_yoga_layout(int force) {
  if (YGNodeIsDirty(yogaRoot) || force) {
    fprintf(stdout, "systemp ========>:  Update Layout\n");
    YGNodeCalculateLayout(yogaRoot, VIEW_WIDTH, VIEW_HEIGHT, YGDirectionLTR);
  }
}

TreeNode *find_node_by_id(int nodeId) {
  return g_hash_table_lookup(nodeIdMap, &nodeId);
}

TreeNode *find_node_at_position(TreeNode *dataNode, YGNodeRef yogaNode, int x,
                                int y) {
  float left = YGNodeLayoutGetLeft(yogaNode);
  float top = YGNodeLayoutGetTop(yogaNode);
  float width = YGNodeLayoutGetWidth(yogaNode);
  float height = YGNodeLayoutGetHeight(yogaNode);

  if (dataNode->node_type == TEXT) {
    return NULL;
  }
  if (x < left || x > left + width || y < top || y > top + height)
    return NULL;

  int localX = x - left;
  int localY = y - top;
  for (int i = 0; i < dataNode->childCount; i++) {
    TreeNode *found = find_node_at_position(
        dataNode->children[i], YGNodeGetChild(yogaNode, i), localX, localY);
    if (found)
      return found;
  }
  return dataNode;
}

void render_text(TTF_Font *font, SDL_Renderer *renderer, TreeNode *node, int x,
                 int y, int w, int h) {

  // 设置文字颜色
  SDL_Color color = {0, 0, 0, 255}; // 黑色文字
  // 渲染文字表面
  SDL_Surface *text_surface =
      TTF_RenderText_Blended_Wrapped(font, node->text, color, w);
  // 创建纹理
  SDL_Texture *text_texture =
      SDL_CreateTextureFromSurface(renderer, text_surface);
  // 设置文字位置
  SDL_Rect text_rect = {x, // x 坐标
                        y, // y 坐标
                        text_surface->w, text_surface->h};
  // 绘制文字
  SDL_RenderCopy(renderer, text_texture, NULL, &text_rect);

  // 清理资源
  SDL_DestroyTexture(text_texture);
  SDL_FreeSurface(text_surface);
}

/*-------------------------------------
 * 渲染系统
 *-----------------------------------*/
void render_tree(TTF_Font *font, SDL_Renderer *renderer, TreeNode *dataNode,
                 int parentX, int parentY) {
  if (!dataNode)
    return;
  YGNodeRef yogaNode = dataNode->yogaNode;

  int x = parentX + (int)YGNodeLayoutGetLeft(yogaNode);
  int y = parentY + (int)YGNodeLayoutGetTop(yogaNode);
  int w = (int)YGNodeLayoutGetWidth(yogaNode);
  int h = (int)YGNodeLayoutGetHeight(yogaNode);

  // 如果是 TEXT 节点，渲染文字
  if (dataNode->node_type == TEXT && dataNode->text) {
    render_text(font, renderer, dataNode, x, y, w, h);
    return;
  }

  // 绘制背景
  Color bg = dataNode->style->backgroundColor;
  // 开启透明
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
  SDL_Rect rect = {x, y, w, h};
  SDL_RenderFillRect(renderer, &rect);

  // 绘制边框
  Color border = (dataNode == selectedNode) ? COLOR_HIGHLIGHT
                                            : dataNode->style->borderColor;
  SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
  SDL_RenderDrawRect(renderer, &rect);

  // 递归渲染子节点
  for (int i = 0; i < dataNode->childCount; i++) {
    render_tree(font, renderer, dataNode->children[i], x, y);
  }
}

static JSValue js_createNode(JSContext *ctx, JSValue this_val, int argc,
                             JSValue *argv) {
  // 创建 C 层对象
  TreeNode *node = create_node(NODE, NULL, 1.0f, 10.0f, YGFlexDirectionRow,
                               YGJustifyFlexStart);
  if (!node)
    return JS_ThrowOutOfMemory(ctx);

  // 包装为 JS 对象
  return wrap_node(ctx, node);
}

static JSValue js_createTextNode(JSContext *ctx, JSValue this_val, int argc,
                                 JSValue *argv) {
  if (argc < 1) {
    return JS_ThrowTypeError(ctx,
                             "createNode requires at least 1 argument: text");
  }
  const char *text = JS_ToCString(ctx, argv[0]);

  // 创建 C 层对象
  TreeNode *node =
      create_node(TEXT, text, 1.0f, 0, YGFlexDirectionRow, YGJustifyFlexStart);
  node->style->borderColor = COLOR_WHITE;
  node->style->backgroundColor = COLOR_TRANSPARENT;

  JS_FreeCString(ctx, text);
  if (!node)
    return JS_ThrowOutOfMemory(ctx);

  // 包装为 JS 对象
  return wrap_node(ctx, node);
}

static JSValue js_appendChild(JSContext *ctx, JSValue this_val, int argc,
                              JSValue *argv) {
  // 参数必须为2个：parent 和 child
  if (argc != 2) {
    return JS_ThrowTypeError(
        ctx, "appendChild requires 2 arguments: parent and child");
  }

  // 解包父节点和子节点
  TreeNode *parent = unwrap_node(ctx, argv[0]);
  TreeNode *child = unwrap_node(ctx, argv[1]);

  if (!parent) {
    return JS_ThrowTypeError(ctx, "Invalid parent node");
  }
  if (!child) {
    return JS_ThrowTypeError(ctx, "Invalid child node");
  }

  // 执行添加操作
  if (append_child(parent, child)) {
    return JS_UNDEFINED;
  } else {
    return JS_ThrowInternalError(ctx, "Failed to append child");
  }
}

// JS 绑定的 addEventListener
static JSValue js_addEventListener(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
  if (argc != 3) {
    return JS_ThrowTypeError(
        ctx, "addEventListener requires 3 arguments: node & type & callback");
  }
  TreeNode *node = unwrap_node(ctx, argv[0]);
  const char *event_type = JS_ToCString(ctx, argv[1]);
  JSValue callback = argv[2];

  // 调用 C 函数时传递 ctx
  add_listener(ctx, node, event_type, callback);
  JS_FreeCString(ctx, event_type);
  return JS_UNDEFINED;
}

// JS 绑定的 removeEventListener
static JSValue js_removeEventListener(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
  if (argc != 3) {
    return JS_ThrowTypeError(
        ctx,
        "removeEventListener requires 3 arguments: node & type & callback");
  }
  TreeNode *node = unwrap_node(ctx, argv[0]);
  const char *event_type = JS_ToCString(ctx, argv[1]);
  JSValue callback = argv[2];

  // 调用 C 函数时传递 ctx
  remove_listener(ctx, node, event_type, callback);
  JS_FreeCString(ctx, event_type);
  return JS_UNDEFINED;
}

static JSValue js_dispatchEvent(JSContext *ctx, JSValueConst this_val, int argc,
                                JSValueConst *argv) {
  if (argc != 2) {
    return JS_ThrowTypeError(ctx,
                             "dispatchEvent requires 2 arguments: node & type");
  }
  TreeNode *node = unwrap_node(ctx, argv[0]);
  const char *event_type = JS_ToCString(ctx, argv[1]);

  // 调用 C 层函数
  dispatch_event(ctx, node, event_type);

  JS_FreeCString(ctx, event_type);
  return JS_UNDEFINED;
}

static JSValue js_removeChild(JSContext *ctx, JSValue this_val, int argc,
                              JSValue *argv) {
  // 参数必须为2个：parent 和 child
  if (argc != 2) {
    return JS_ThrowTypeError(
        ctx, "appendChild requires 2 arguments: parent and child");
  }

  // 解包父节点和子节点
  TreeNode *parent = unwrap_node(ctx, argv[0]);
  TreeNode *child = unwrap_node(ctx, argv[1]);

  if (!parent) {
    return JS_ThrowTypeError(ctx, "Invalid parent node");
  }
  if (!child) {
    return JS_ThrowTypeError(ctx, "Invalid child node");
  }

  // 执行添加操作
  if (remove_child(ctx, parent, child)) {
    return JS_UNDEFINED;
  } else {
    return JS_ThrowInternalError(ctx, "Failed to remove child");
  }
}

static JSValue js_insertBefore(JSContext *ctx, JSValue this_val, int argc,
                               JSValue *argv) {
  // 参数必须为3个：parent 和 child, ref
  if (argc != 3) {
    return JS_ThrowTypeError(
        ctx, "appendChild requires 3 arguments: parent and child");
  }

  // 解包父节点和子节点
  TreeNode *parent = unwrap_node(ctx, argv[0]);
  TreeNode *child = unwrap_node(ctx, argv[1]);
  TreeNode *ref = unwrap_node(ctx, argv[2]);

  if (!parent) {
    return JS_ThrowTypeError(ctx, "Invalid parent node");
  }
  if (!child) {
    return JS_ThrowTypeError(ctx, "Invalid child node");
  }

  // 执行添加操作
  if (insert_before(parent, child, ref)) {
    return JS_UNDEFINED;
  } else {
    return JS_ThrowInternalError(ctx, "Failed to insert child");
  }
}

static JSValue js_setAttribute(JSContext *ctx, JSValue this_val, int argc,
                               JSValue *argv) {
  // 参数必须为3个：node 和 key, value
  if (argc != 3) {
    return JS_ThrowTypeError(
        ctx, "appendChild requires 3 arguments: parent and child");
  }

  // 解包父节点和子节点
  TreeNode *node = unwrap_node(ctx, argv[0]);

  if (!node) {
    return JS_ThrowTypeError(ctx, "Invalid node parameter");
  }

  // 转换属性名（第二个参数）
  const char *attr = JS_ToCString(ctx, argv[1]);
  if (!attr) {
    return JS_ThrowTypeError(ctx, "Invalid attribute name");
  }

  // 转换属性值（第三个参数）
  const char *value = JS_ToCString(ctx, argv[2]);
  if (!value) {
    JS_FreeCString(ctx, attr); // 释放已分配的属性名
    return JS_ThrowTypeError(ctx, "Invalid attribute value");
  }

  // 调用底层设置属性逻辑
  int ret = set_attribute(node, attr, value);

  // 释放字符串资源
  JS_FreeCString(ctx, attr);
  JS_FreeCString(ctx, value);

  // 处理操作结果
  if (ret == 1) {
    return JS_UNDEFINED;
  } else {
    return JS_ThrowInternalError(ctx, "Failed to set attribute '%s'", attr);
  }
}

static JSValue js_setTextContent(JSContext *ctx, JSValue this_val, int argc,
                                 JSValue *argv) {
  // 检查参数数量
  if (argc != 2) {
    return JS_ThrowTypeError(
        ctx, "setTextContent requires exactly 2 arguments: node and text");
  }

  // 解包 TreeNode
  TreeNode *node = unwrap_node(ctx, argv[0]);
  if (!node) {
    return JS_ThrowTypeError(ctx, "Invalid node parameter");
  }

  // 检查节点类型是否为 TEXT
  if (node->node_type != TEXT) {
    return JS_ThrowTypeError(ctx, "Node is not a TEXT node");
  }

  // 获取新的文字内容
  const char *new_text = JS_ToCString(ctx, argv[1]);
  if (!new_text) {
    return JS_ThrowTypeError(ctx, "Invalid text parameter");
  }

  // 更新节点的文字内容
  set_node_text(node, new_text);

  // 释放 C 字符串资源
  JS_FreeCString(ctx, new_text);

  return JS_UNDEFINED;
}

/*-------------------------------------
 * 主程序
 *-----------------------------------*/
int main(int argc, char *argv[]) {

  if (argc < 2) {
    fprintf(stderr, "Usage: %s <js-file>\n", argv[0]);
    return 1;
  }

  char *code = NULL;
  JSRuntime *rt = NULL;
  JSContext *ctx = NULL;
  uv_loop_t *loop = uv_default_loop();
  JSValue val = JS_UNDEFINED;

  // 读取文件
  int len = readfile(argv[1], &code);
  if (len == -1) {
    fprintf(stderr, "Error reading file: %s\n", argv[1]);
    cleanup_resources(NULL, NULL, loop, code, val);
    return 1;
  }

  nodeIdMap = g_hash_table_new(g_int_hash, g_int_equal);
  root_data = create_node(NODE, NULL, 1.0f, 10.0f, YGFlexDirectionRow,
                          YGJustifyFlexStart);
  root_data->style->backgroundColor = parse_color("#F0F0F0"); // 根节点浅灰背景
  yogaRoot = root_data->yogaNode;

  // 初始化 QuickJS 运行时
  rt = JS_NewRuntime();
  if (!rt) {
    fprintf(stderr, "Error creating JS runtime\n");
    cleanup_resources(NULL, NULL, loop, code, val);
    return 1;
  }

  // 创建上下文
  ctx = JS_NewContext(rt);
  if (!ctx) {
    fprintf(stderr, "Error creating JS context\n");
    cleanup_resources(rt, NULL, loop, code, val);
    return 1;
  }

  // 初始化标准库
  js_std_init_handlers(rt);
  js_std_add_helpers(ctx, 0, NULL);

  JSValue js_document = wrap_node(ctx, root_data);

  // 注册全局函数
  JSValue global = JS_GetGlobalObject(ctx);
  JS_SetPropertyStr(ctx, global, "createNode",
                    JS_NewCFunction(ctx, js_createNode, "createNode", 0));
  JS_SetPropertyStr(ctx, global, "appendChild",
                    JS_NewCFunction(ctx, js_appendChild, "appendChild", 2));
  JS_SetPropertyStr(ctx, global, "removeChild",
                    JS_NewCFunction(ctx, js_removeChild, "removeChild", 2));
  JS_SetPropertyStr(ctx, global, "insertBefore",
                    JS_NewCFunction(ctx, js_insertBefore, "insertBefore", 3));
  JS_SetPropertyStr(ctx, global, "setAttribute",
                    JS_NewCFunction(ctx, js_setAttribute, "setAttribute", 3));
  JS_SetPropertyStr(
      ctx, global, "createTextNode",
      JS_NewCFunction(ctx, js_createTextNode, "createTextNode", 1));
  JS_SetPropertyStr(
      ctx, global, "setTextContent",
      JS_NewCFunction(ctx, js_setTextContent, "setTextContent", 2));
  JS_SetPropertyStr(
      ctx, global, "addEventListener",
      JS_NewCFunction(ctx, js_addEventListener, "addEventListener", 3));
  JS_SetPropertyStr(
      ctx, global, "removeEventListener",
      JS_NewCFunction(ctx, js_removeEventListener, "removeEventListener", 3));

  JS_SetPropertyStr(ctx, global, "dispatchEvent",
                    JS_NewCFunction(ctx, js_dispatchEvent, "dispatchEvent", 2));
  JS_SetPropertyStr(ctx, global, "document", js_document);
  JS_SetPropertyStr(ctx, global, "setTimeout",
                    JS_NewCFunction(ctx, js_setTimeout, "setTimeout", 2));
  JS_SetPropertyStr(ctx, global, "setInterval",
                    JS_NewCFunction(ctx, js_setInterval, "setInterval", 2));
  JS_SetPropertyStr(ctx, global, "clearTimeout",
                    JS_NewCFunction(ctx, js_clearTimer, "clearTimeout", 1));
  JS_SetPropertyStr(ctx, global, "clearInterval",
                    JS_NewCFunction(ctx, js_clearTimer, "clearInterval", 1));
  JS_FreeValue(ctx, global);

  // 执行脚本
  val = JS_Eval(ctx, code, len, argv[1], JS_EVAL_TYPE_MODULE);
  if (JS_IsException(val)) {
    js_std_dump_error(ctx);
    cleanup_resources(rt, ctx, loop, code, val);
    return 1;
  }

  SDL_Init(SDL_INIT_VIDEO);
  TTF_Init();
  TTF_Font *font = TTF_OpenFont("Arial.ttf", 24);
  if (!font) {
    SDL_Log("TTF_OpenFont failed: %s", TTF_GetError());
    TTF_Quit();
    SDL_Quit();
    return 1;
  }
  SDL_Window *window = SDL_CreateWindow(
      "树形布局编辑器 - A:添加 D:删除 I:插入 F:切换方向 1-3:颜色 R:重置 "
      "N:高亮下一节点 S:设置属性",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, VIEW_WIDTH, VIEW_HEIGHT,
      SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
  SDL_Renderer *renderer =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

  int quit = 0;
  while (!quit) {
    // 处理JavaScript异步任务
    int js_pending;
    do {
      JSContext *ctx;
      js_pending = JS_ExecutePendingJob(rt, &ctx);
      if (js_pending < 0) {
        js_std_dump_error(ctx);
        quit = 1; // JS执行出错时退出
        break;
      }
    } while (js_pending > 0);

    js_std_loop(ctx);

    if (quit)
      break;

    // 处理libuv事件（非阻塞模式）
    uv_run(loop, UV_RUN_NOWAIT);

    // if (!uv_loop_alive(loop))
    //   break;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_QUIT:
        uv_walk(loop, close_timers_cb, NULL);
        uv_run(loop, UV_RUN_NOWAIT);
        quit = 1;
        break;

      case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
          VIEW_WIDTH = event.window.data1;
          VIEW_HEIGHT = event.window.data2;
          update_yoga_layout(1);
        }
        break;

      case SDL_MOUSEBUTTONDOWN: {
        int x = event.button.x;
        int y = event.button.y;
        selectedNode = find_node_at_position(root_data, yogaRoot, x, y);
        dispatch_event(ctx, selectedNode, "click");
        break;
      }

      case SDL_KEYDOWN:
        if (selectedNode) {
          switch (event.key.keysym.sym) {
          case SDLK_a: { // 添加子节点
            TreeNode *child = create_node(
                NODE, NULL, 1.0f, 5.0f, YGFlexDirectionRow, YGJustifyFlexStart);
            append_child(selectedNode, child);
            break;
          }
          case SDLK_d: {
            // 删除节点
            if (selectedNode->parent) {
              if (remove_child(ctx, selectedNode->parent, selectedNode)) {
                selectedNode = NULL;
              }
            }
            break;
          }
          case SDLK_i: { // 插入节点
            if (selectedNode->parent) {
              TreeNode *newNode =
                  create_node(NODE, NULL, 1.0f, 5.0f, YGFlexDirectionRow,
                              YGJustifyFlexStart);
              insert_before(selectedNode->parent, newNode, selectedNode);
            }
            break;
          }
          case SDLK_s: { // 示例：按S键设置属性
            // 示例设置多个属性
            set_attribute(selectedNode, "flex", "2.0");
            set_attribute(selectedNode, "backgroundColor", "#FFA500");
            break;
          }
          case SDLK_n: { // 高亮下一个创建的节点
            TreeNode *targetNode =
                find_node_by_id(selectedNode->id + 1); // 假设要查找ID为1的节点
            selectedNode = targetNode;
            break;
          }
          case SDLK_f: {
            // 切换方向
            set_attribute(
                selectedNode, "flexDirection",
                (selectedNode->style->flexDirection == YGFlexDirectionRow)
                    ? "column"
                    : "row");
            break;
          }
          case SDLK_1: // 红色主题
            set_attribute(selectedNode, "backgroundColor", "#FF0000");
            break;
          case SDLK_2: // 绿色主题
            set_attribute(selectedNode, "backgroundColor", "#00FF00");
            break;
          case SDLK_3: // 蓝色主题
            set_attribute(selectedNode, "backgroundColor", "#0000FF");
            break;
          case SDLK_r: // 重置样式
            set_attribute(selectedNode, "backgroundColor", "#FFFFFF");
            set_attribute(selectedNode, "flex", "1.0");
            break;
          }
        }
        break;
      }
    }

    update_yoga_layout(0);
    SDL_SetRenderDrawColor(renderer, 240, 240, 240, 255);
    SDL_RenderClear(renderer);
    render_tree(font, renderer, root_data, 0, 0);
    SDL_RenderPresent(renderer);
    SDL_Delay(16);
  }

  // 正常退出时的清理
  free_tree(ctx, root_data);
  cleanup_resources(rt, ctx, loop, code, val);
  g_hash_table_destroy(nodeIdMap);
  TTF_CloseFont(font);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  TTF_Quit();
  SDL_Quit();
  return 0;
}