#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <yoga/Yoga.h>

// 定义树形节点的数据结构
typedef struct TreeNode {
  int id;                     // 节点唯一标识
  float flex;                 // flex 值
  float margin;               // 外边距
  YGJustify justifyContent;   // 主轴对齐方式
  int childCount;             // 子节点数量
  struct TreeNode **children; // 子节点数组
} TreeNode;

// 递归创建 Yoga 节点树
YGNodeRef create_yoga_tree(TreeNode *data) {
  YGNodeRef node = YGNodeNew();

  // 设置节点样式
  YGNodeStyleSetFlex(node, data->flex);
  YGNodeStyleSetMargin(node, YGEdgeAll, data->margin);
  YGNodeStyleSetJustifyContent(node, data->justifyContent);

  // 递归创建子节点
  for (int i = 0; i < data->childCount; i++) {
    YGNodeRef child = create_yoga_tree(data->children[i]);
    YGNodeInsertChild(node, child, i);
  }
  return node;
}

// 递归渲染 Yoga 节点树
void render_tree(SDL_Renderer *renderer, YGNodeRef node, int depth) {
  // 根据层级设置颜色（根节点为蓝色，子节点为红色）
  SDL_Color color = {0, 0, 255, 255}; // 默认蓝色
  if (depth > 0) {
    color = (SDL_Color){255, 0, 0, 255}; // 子节点红色
  }

  // 绘制当前节点边框
  SDL_Rect rect = {.x = (int)YGNodeLayoutGetLeft(node),
                   .y = (int)YGNodeLayoutGetTop(node),
                   .w = (int)YGNodeLayoutGetWidth(node),
                   .h = (int)YGNodeLayoutGetHeight(node)};
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderDrawRect(renderer, &rect);

  // 递归渲染子节点
  for (int i = 0; i < YGNodeGetChildCount(node); i++) {
    render_tree(renderer, YGNodeGetChild(node, i), depth + 1);
  }
}

// 生成随机树形结构
TreeNode *generate_random_tree(int max_depth, int current_depth) {
  if (current_depth > max_depth)
    return NULL;

  TreeNode *node = malloc(sizeof(TreeNode));
  node->id = rand();
  node->flex = (float)(rand() % 3 + 1);           // flex 值 1~3
  node->margin = (float)(rand() % 10);            // 外边距 0~9
  node->justifyContent = (YGJustify)(rand() % 5); // 随机对齐方式
  node->childCount = (current_depth < max_depth) ? (rand() % 3) : 0;

  node->children = malloc(sizeof(TreeNode *) * node->childCount);
  for (int i = 0; i < node->childCount; i++) {
    node->children[i] = generate_random_tree(max_depth, current_depth + 1);
  }

  return node;
}

int main() {
  // ------------------------------
  // 步骤 1: 定义树形结构（硬编码示例）
  // ------------------------------
  TreeNode *root_data = generate_random_tree(3, 0);

  // ------------------------------
  // 步骤 2: 创建 Yoga 节点树并计算布局
  // ------------------------------
  YGNodeRef root = create_yoga_tree(root_data);
  YGNodeStyleSetWidth(root, 500);
  YGNodeStyleSetHeight(root, 300);
  YGNodeCalculateLayout(root, 500, 300, YGDirectionLTR);

  // ------------------------------
  // 步骤 3: 初始化 SDL2 窗口和渲染器
  // ------------------------------
  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window *window =
      SDL_CreateWindow("Yoga 树形布局渲染", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, 500, 300, SDL_WINDOW_SHOWN);
  SDL_Renderer *renderer =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

  // ------------------------------
  // 步骤 4: 主循环（渲染 + 事件处理）
  // ------------------------------
  bool quit = 0;
  while (!quit) {
    // 处理事件
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        quit = 1;
      }
      if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_SPACE) {
          // 重新生成随机树
          YGNodeFreeRecursive(root);
          root = create_yoga_tree(generate_random_tree(3, 0));
          YGNodeCalculateLayout(root, 500, 300, YGDirectionLTR);
        }
      }
    }

    // 清空屏幕为白色背景
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);

    // 渲染树形结构
    render_tree(renderer, root, 0);

    // 更新窗口
    SDL_RenderPresent(renderer);
    SDL_Delay(16); // 约 60 FPS
  }

  // ------------------------------
  // 步骤 5: 清理资源
  // ------------------------------
  YGNodeFreeRecursive(root);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}