#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yoga/Yoga.h>

// 定义树形节点的数据结构
typedef struct TreeNode {
  int id;
  float flex;
  float margin;
  YGFlexDirection flexDirection; // 新增方向属性
  YGJustify justifyContent;
  int childCount;
  struct TreeNode **children;
  struct TreeNode *parent;
} TreeNode;

// 全局变量
TreeNode *selectedNode = NULL; // 当前选中的节点
YGNodeRef yogaRoot = NULL;     // Yoga 根节点
TreeNode *root_data = NULL;    // 数据根节点
int nextNodeId = 0;            // 用于生成唯一的节点 ID
int VIEW_WIDTH = 1000;
int VIEW_HEIGHT = 600;

// ------------------------------
// 核心方法实现
// ------------------------------

// 创建新节点
TreeNode *create_node(float flex, float margin, YGFlexDirection flexDirection,
                      YGJustify justifyContent) {
  TreeNode *node = malloc(sizeof(TreeNode));
  node->id = nextNodeId++;
  node->flex = flex;
  node->margin = margin;
  node->flexDirection = flexDirection;
  node->justifyContent = justifyContent;
  node->childCount = 0;
  node->children = NULL;
  node->parent = NULL;
  return node;
}

// 递归释放节点及其子树内存
void free_tree(TreeNode *node) {
  for (int i = 0; i < node->childCount; i++) {
    free_tree(node->children[i]);
  }
  free(node->children);
  free(node);
}

// 追加子节点（appendChild）
bool append_child(TreeNode *parent, TreeNode *child) {
  if (!parent || !child || child->parent != NULL) {
    fprintf(stderr, "错误：无法追加节点，父节点或子节点无效\n");
    return 0;
  }

  parent->children =
      realloc(parent->children, sizeof(TreeNode *) * (parent->childCount + 1));
  parent->children[parent->childCount] = child;
  parent->childCount++;
  child->parent = parent;
  return 1;
}

// 移除子节点（removeChild）
bool remove_child(TreeNode *parent, TreeNode *child) {
  if (!parent || !child || parent != child->parent) {
    fprintf(stderr, "错误：无法移除节点，父子关系不匹配\n");
    return 0;
  }

  // 查找子节点位置
  int index = -1;
  for (int i = 0; i < parent->childCount; i++) {
    if (parent->children[i] == child) {
      index = i;
      break;
    }
  }
  if (index == -1) {
    fprintf(stderr, "错误：目标节点不在父节点的子节点列表中\n");
    return 0;
  }

  // 释放目标节点及其子树
  free_tree(child);

  // 调整数组
  memmove(&parent->children[index], &parent->children[index + 1],
          sizeof(TreeNode *) * (parent->childCount - index - 1));
  parent->childCount--;
  parent->children =
      realloc(parent->children, sizeof(TreeNode *) * parent->childCount);
  return 1;
}

// 插入到指定位置（insertBefore）
bool insert_before(TreeNode *parent, TreeNode *newChild, TreeNode *refChild) {
  if (!parent || !newChild || !refChild || parent != refChild->parent ||
      newChild->parent != NULL) {
    fprintf(stderr, "错误：插入操作参数无效\n");
    return 0;
  }

  // 查找参考节点位置
  int index = -1;
  for (int i = 0; i < parent->childCount; i++) {
    if (parent->children[i] == refChild) {
      index = i;
      break;
    }
  }
  if (index == -1) {
    fprintf(stderr, "错误：参考节点不在父节点的子节点列表中\n");
    return 0;
  }

  // 扩展数组并插入新节点
  parent->children =
      realloc(parent->children, sizeof(TreeNode *) * (parent->childCount + 1));
  memmove(&parent->children[index + 1], &parent->children[index],
          sizeof(TreeNode *) * (parent->childCount - index));
  parent->children[index] = newChild;
  parent->childCount++;
  newChild->parent = parent;
  return 1;
}

// ------------------------------
// Yoga 布局与渲染逻辑
// ------------------------------

// 递归创建 Yoga 树
YGNodeRef create_yoga_tree(TreeNode *data) {
  YGNodeRef node = YGNodeNew();
  YGNodeStyleSetFlex(node, data->flex);
  YGNodeStyleSetMargin(node, YGEdgeAll, data->margin); // 设置margin
  YGNodeStyleSetFlexDirection(node, data->flexDirection);
  YGNodeStyleSetJustifyContent(node, data->justifyContent);

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
  int innerWidth = VIEW_WIDTH - 2 *  root_data->margin;
  int innerHeight = VIEW_HEIGHT - 2 *  root_data->margin;
  YGNodeStyleSetWidth(yogaRoot, innerWidth);
  YGNodeStyleSetHeight(yogaRoot, innerHeight);
  YGNodeCalculateLayout(yogaRoot, innerWidth, innerHeight, YGDirectionLTR);
}

// 通过坐标查找节点
TreeNode *find_node_at_position(TreeNode *dataNode, YGNodeRef yogaNode, int x,
                                int y) {
  float left = YGNodeLayoutGetLeft(yogaNode);
  float top = YGNodeLayoutGetTop(yogaNode);
  float width = YGNodeLayoutGetWidth(yogaNode);
  float height = YGNodeLayoutGetHeight(yogaNode);

  // 检查当前节点是否包含坐标
  if (x >= left && x <= left + width && y >= top && y <= top + height) {
    // 转换为相对坐标
    int localX = x - left;
    int localY = y - top;

    // 递归检查子节点
    for (int i = 0; i < dataNode->childCount; i++) {
      TreeNode *found = find_node_at_position(
          dataNode->children[i], YGNodeGetChild(yogaNode, i), localX, localY);
      if (found)
        return found;
    }
    return dataNode; // 没有子节点命中，返回当前节点
  }
  return NULL;
}

// 递归渲染树形结构
// 修改后的渲染函数（关键修正）
void render_tree(SDL_Renderer *renderer, YGNodeRef yogaNode, TreeNode *dataNode,
                 int parentAbsX, int parentAbsY) {
  // 计算绝对坐标（叠加父节点位置）
  float nodeLeft = YGNodeLayoutGetLeft(yogaNode);
  float nodeTop = YGNodeLayoutGetTop(yogaNode);
  int absX = parentAbsX + (int)nodeLeft;
  int absY = parentAbsY + (int)nodeTop;

  float width = YGNodeLayoutGetWidth(yogaNode);
  float height = YGNodeLayoutGetHeight(yogaNode);

  // 绘制当前节点边框
  SDL_Rect rect = {.x = absX, .y = absY, .w = (int)width, .h = (int)height};
  SDL_SetRenderDrawColor(renderer, (dataNode == selectedNode) ? 255 : 0,
                         (dataNode == selectedNode) ? 255 : 0, 0, 255);
  SDL_RenderDrawRect(renderer, &rect);

  // 递归渲染子节点
  for (int i = 0; i < dataNode->childCount; i++) {
    render_tree(renderer, YGNodeGetChild(yogaNode, i), dataNode->children[i],
                absX, absY);
  }
}

// ------------------------------
// 主程序逻辑
// ------------------------------

int main() {
  // 初始化根节点
  root_data = create_node(1.0f, 5.0f, YGFlexDirectionRow, YGJustifySpaceAround);

  // 初始化 SDL
  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window *window = SDL_CreateWindow(
      "动态树形布局编辑器 (A: 追加, D: 删除, I: 插入, F: 切换方向)",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1000, 600,
      SDL_WINDOW_SHOWN);
  SDL_Renderer *renderer =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

  // 初始布局
  update_yoga_layout();

  // 主循环
  bool quit = 0;
  while (!quit) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_QUIT:
        quit = 1;
        break;
      case SDL_MOUSEBUTTONDOWN: {
        // 鼠标点击选择节点
        int x = event.button.x;
        int y = event.button.y;
        selectedNode = find_node_at_position(root_data, yogaRoot, x, y);
        break;
      }
      case SDL_KEYDOWN:
        if (selectedNode) {
          switch (event.key.keysym.sym) {
          case SDLK_a: // 追加子节点
            if (selectedNode) {
              // 新子节点默认为横向排列
              TreeNode *newChild = create_node(1.0f, 5.0f, YGFlexDirectionRow,
                                               YGJustifyFlexStart);
              if (append_child(selectedNode, newChild)) {
                update_yoga_layout();
              }
            }
            break;
          case SDLK_d: // 删除节点
            if (selectedNode->parent) {
              if (remove_child(selectedNode->parent, selectedNode)) {
                selectedNode = NULL; // 清空选中状态
                update_yoga_layout();
              }
            }
            break;
          case SDLK_i: // 插入到同级前面
            if (selectedNode->parent) {
              // 新节点默认为横向排列
              TreeNode *newNode = create_node(1.0f, 5.0f, YGFlexDirectionRow,
                                              YGJustifyFlexStart);
              if (insert_before(selectedNode->parent, newNode, selectedNode)) {
                update_yoga_layout();
              }
            }
            break;
          case SDLK_f: // 切换节点方向
            if (selectedNode) {
              // 切换 flexDirection
              selectedNode->flexDirection =
                  selectedNode->flexDirection == YGFlexDirectionRow
                      ? YGFlexDirectionColumn
                      : YGFlexDirectionRow;
              update_yoga_layout();
            }
            break;
          }
        }
        break;
      }
    }

    // 渲染
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);
    if (yogaRoot) {
      render_tree(renderer, yogaRoot, root_data, 0, 0);
    }
    SDL_RenderPresent(renderer);
    SDL_Delay(16);
  }

  // 清理资源
  free_tree(root_data);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}