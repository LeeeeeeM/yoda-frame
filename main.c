#include <SDL2/SDL.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yoga/Yoga.h>

/*-------------------------------------
 * 颜色结构体定义
 *-----------------------------------*/
typedef struct {
  Uint8 r, g, b, a;
} Color;

// 预设颜色
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
  float borderWidth;
} NodeStyle;

/*-------------------------------------
 * 树节点结构体定义
 *-----------------------------------*/
typedef struct TreeNode {
  int id;
  NodeStyle *style;
  int childCount;
  struct TreeNode **children;
  struct TreeNode *parent;
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

TreeNode *create_node(float flex, float margin, YGFlexDirection flexDirection,
                      YGJustify justifyContent) {
  TreeNode *node = (TreeNode *)malloc(sizeof(TreeNode));
  node->id = nextNodeId++;
  g_hash_table_insert(nodeIdMap, &node->id, node);

  node->style = (NodeStyle *)malloc(sizeof(NodeStyle));
  node->style->flex = flex;
  node->style->margin = margin;
  node->style->flexDirection = flexDirection;
  node->style->justifyContent = justifyContent;

  // 初始化白底黑边
  node->style->backgroundColor = COLOR_WHITE;
  node->style->borderColor = COLOR_BLACK;
  node->style->borderWidth = 1.0f;

  node->childCount = 0;
  node->children = NULL;
  node->parent = NULL;
  return node;
}

void free_tree(TreeNode *node) {
  if (node) {
    for (int i = 0; i < node->childCount; i++) {
      free_tree(node->children[i]);
    }
    g_hash_table_remove(nodeIdMap, &node->id);
    free(node->children);
    free(node->style);
    free(node);
  }
}

int append_child(TreeNode *parent, TreeNode *child) {
  if (!parent || !child || child->parent)
    return 0;
  parent->children =
      realloc(parent->children, sizeof(TreeNode *) * (parent->childCount + 1));
  parent->children[parent->childCount++] = child;
  child->parent = parent;
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
  return 1;
}

int remove_child(TreeNode *parent, TreeNode *child) {
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

  free_tree(child);
  memmove(&parent->children[index], &parent->children[index + 1],
          sizeof(TreeNode *) * (parent->childCount - index - 1));
  parent->childCount--;
  parent->children =
      realloc(parent->children, sizeof(TreeNode *) * parent->childCount);
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
    return 1;
  } else if (strcmp(attr, "margin") == 0) {
    float margin = atof(value);
    node->style->margin = margin;
    return 1;
  } else if (strcmp(attr, "flexDirection") == 0) {
    if (strcmp(value, "row") == 0) {
      node->style->flexDirection = YGFlexDirectionRow;
    } else if (strcmp(value, "column") == 0) {
      node->style->flexDirection = YGFlexDirectionColumn;
    } else {
      return 0;
    }
    return 1;
  } else if (strcmp(attr, "justifyContent") == 0) {
    if (strcmp(value, "flex-start") == 0) {
      node->style->justifyContent = YGJustifyFlexStart;
    } else if (strcmp(value, "center") == 0) {
      node->style->justifyContent = YGJustifyCenter;
    } else if (strcmp(value, "flex-end") == 0) {
      node->style->justifyContent = YGJustifyFlexEnd;
    } else if (strcmp(value, "space-between") == 0) {
      node->style->justifyContent = YGJustifySpaceBetween;
    } else if (strcmp(value, "space-around") == 0) {
      node->style->justifyContent = YGJustifySpaceAround;
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
  } else if (strcmp(attr, "borderWidth") == 0) {
    float width = atof(value);
    if (width >= 0) {
      node->style->borderWidth = width;
      return 1;
    }
    return 0;
  }

  return 0; // 未知属性
}

/*-------------------------------------
 * Yoga布局系统
 *-----------------------------------*/
YGNodeRef create_yoga_tree(TreeNode *data) {
  YGNodeRef node = YGNodeNew();
  YGNodeStyleSetFlex(node, data->style->flex);
  YGNodeStyleSetMargin(node, YGEdgeAll, data->style->margin);
  YGNodeStyleSetFlexDirection(node, data->style->flexDirection);
  YGNodeStyleSetJustifyContent(node, data->style->justifyContent);

  for (int i = 0; i < data->childCount; i++) {
    YGNodeRef child = create_yoga_tree(data->children[i]);
    YGNodeInsertChild(node, child, i);
  }
  return node;
}

void update_yoga_layout() {
  if (yogaRoot)
    YGNodeFreeRecursive(yogaRoot);
  yogaRoot = create_yoga_tree(root_data);
  float margin = root_data->style->margin;
  YGNodeStyleSetWidth(yogaRoot, VIEW_WIDTH - 2 * margin);
  YGNodeStyleSetHeight(yogaRoot, VIEW_HEIGHT - 2 * margin);
  YGNodeCalculateLayout(yogaRoot, VIEW_WIDTH, VIEW_HEIGHT, YGDirectionLTR);
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

/*-------------------------------------
 * 渲染系统
 *-----------------------------------*/
void render_tree(SDL_Renderer *renderer, YGNodeRef yogaNode, TreeNode *dataNode,
                 int parentX, int parentY) {
  int absX = parentX + (int)YGNodeLayoutGetLeft(yogaNode);
  int absY = parentY + (int)YGNodeLayoutGetTop(yogaNode);
  int width = (int)YGNodeLayoutGetWidth(yogaNode);
  int height = (int)YGNodeLayoutGetHeight(yogaNode);

  // 绘制背景
  Color bg = dataNode->style->backgroundColor;
  SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
  SDL_Rect bgRect = {absX, absY, width, height};
  SDL_RenderFillRect(renderer, &bgRect);

  // 绘制边框（选中时用红色高亮）
  Color border = (dataNode == selectedNode) ? COLOR_HIGHLIGHT
                                            : dataNode->style->borderColor;
  SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
  SDL_Rect borderRect = {absX, absY, width, height};
  SDL_RenderDrawRect(renderer, &borderRect);

  // 递归渲染子节点
  for (int i = 0; i < dataNode->childCount; i++) {
    render_tree(renderer, YGNodeGetChild(yogaNode, i), dataNode->children[i],
                absX, absY);
  }
}

/*-------------------------------------
 * 主程序
 *-----------------------------------*/
int main() {
  nodeIdMap = g_hash_table_new(g_int_hash, g_int_equal);
  root_data =
      create_node(1.0f, 10.0f, YGFlexDirectionRow, YGJustifySpaceAround);
  root_data->style->backgroundColor = parse_color("#F0F0F0"); // 根节点浅灰背景

  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window *window = SDL_CreateWindow(
      "树形布局编辑器 - A:添加 D:删除 I:插入 F:切换方向 1-3:颜色 R:重置 N:高亮下一节点 S:设置属性",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, VIEW_WIDTH, VIEW_HEIGHT,
      SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
  SDL_Renderer *renderer =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

  update_yoga_layout();

  int quit = 0;
  while (!quit) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_QUIT:
        quit = 1;
        break;

      case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
          VIEW_WIDTH = event.window.data1;
          VIEW_HEIGHT = event.window.data2;
          update_yoga_layout();
        }
        break;

      case SDL_MOUSEBUTTONDOWN: {
        int x = event.button.x;
        int y = event.button.y;
        selectedNode = find_node_at_position(root_data, yogaRoot, x, y);
        break;
      }

      case SDL_KEYDOWN:
        if (selectedNode) {
          switch (event.key.keysym.sym) {
          case SDLK_a: { // 添加子节点
            TreeNode *child = create_node(1.0f, 5.0f, YGFlexDirectionColumn,
                                          YGJustifyFlexStart);
            if (append_child(selectedNode, child))
              update_yoga_layout();
            break;
          }
          case SDLK_d: // 删除节点
            if (selectedNode->parent) {
              if (remove_child(selectedNode->parent, selectedNode)) {
                selectedNode = NULL;
                update_yoga_layout();
              }
            }
            break;
          case SDLK_i: { // 插入节点
            if (selectedNode->parent) {
              TreeNode *newNode = create_node(1.0f, 5.0f, YGFlexDirectionRow,
                                              YGJustifyFlexStart);
              if (insert_before(selectedNode->parent, newNode, selectedNode)) {
                update_yoga_layout();
              }
            }
            break;
          }
          case SDLK_s: { // 示例：按S键设置属性
            // 示例设置多个属性
            set_attribute(selectedNode, "flex", "2.0");
            set_attribute(selectedNode, "backgroundColor", "#FFA500");
            set_attribute(selectedNode, "borderWidth", "3.0");
            update_yoga_layout();
            break;
          }
          case SDLK_n: { // 高亮下一个创建的节点
            if (selectedNode) {
                TreeNode *targetNode = find_node_by_id(selectedNode->id + 1); // 假设要查找ID为1的节点
                selectedNode = targetNode;
                update_yoga_layout();
            }
            break;
        }
          case SDLK_f: // 切换方向
            selectedNode->style->flexDirection =
                (selectedNode->style->flexDirection == YGFlexDirectionRow)
                    ? YGFlexDirectionColumn
                    : YGFlexDirectionRow;
            update_yoga_layout();
            break;
          case SDLK_1: // 红色主题
            selectedNode->style->backgroundColor = COLOR_RED;
            break;
          case SDLK_2: // 绿色主题
            selectedNode->style->backgroundColor = parse_color("#00FF00");
            break;
          case SDLK_3: // 蓝色主题
            selectedNode->style->backgroundColor = parse_color("#0000FF");
            break;
          case SDLK_r: // 重置样式
            selectedNode->style->backgroundColor = COLOR_WHITE;
            selectedNode->style->borderColor = COLOR_BLACK;
            selectedNode->style->borderWidth = 1.0f;
            selectedNode->style->flex = 1.0f;
            update_yoga_layout();
            break;
          }
        }
        break;
      }
    }

    SDL_SetRenderDrawColor(renderer, 240, 240, 240, 255);
    SDL_RenderClear(renderer);
    if (yogaRoot)
      render_tree(renderer, yogaRoot, root_data, 0, 0);
    SDL_RenderPresent(renderer);
    SDL_Delay(16);
  }

  free_tree(root_data);
  g_hash_table_destroy(nodeIdMap);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}