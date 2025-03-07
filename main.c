#include <SDL2/SDL.h>
#include <yoga/Yoga.h>

// 递归渲染 Yoga 节点树
void render_tree(SDL_Renderer *renderer, YGNodeRef node, float parent_content_x,
                 float parent_content_y) {
  // 获取节点布局信息
  float width = YGNodeLayoutGetWidth(node);
  float height = YGNodeLayoutGetHeight(node);
  float left = YGNodeLayoutGetLeft(node);
  float top = YGNodeLayoutGetTop(node);

  // 计算当前节点实际位置
  float node_x = parent_content_x + left;
  float node_y = parent_content_y + top;

  // 绘制节点边框
  SDL_Rect rect = {(int)node_x, (int)node_y, (int)width, (int)height};
  SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
  SDL_RenderDrawRect(renderer, &rect);

  // 计算当前节点的内容区域起始位置（包含padding）
  float padding_left = YGNodeLayoutGetPadding(node, YGEdgeLeft);
  float padding_top = YGNodeLayoutGetPadding(node, YGEdgeTop);
  float content_x = node_x + padding_left;
  float content_y = node_y + padding_top;

  // 递归渲染子节点
  for (uint32_t i = 0; i < YGNodeGetChildCount(node); i++) {
    render_tree(renderer, YGNodeGetChild(node, i), content_x, content_y);
  }
}

int main() {
  // 初始化 SDL
  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window *window =
      SDL_CreateWindow("Yoga Layout Demo", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, 800, 600, SDL_WINDOW_SHOWN);
  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);

  // 创建 Yoga 根节点
  YGNodeRef root = YGNodeNew();
  YGNodeStyleSetFlexDirection(root, YGFlexDirectionRow);
  YGNodeStyleSetWidth(root, 800);
  YGNodeStyleSetHeight(root, 600);
  YGNodeStyleSetPadding(root, YGEdgeAll, 10);

  // 创建子节点1
  YGNodeRef child1 = YGNodeNew();
  YGNodeStyleSetWidth(child1, 200);
  YGNodeStyleSetHeight(child1, 100);
  YGNodeStyleSetMargin(child1, YGEdgeAll, 10);
  YGNodeInsertChild(root, child1, 0);

  // 创建子节点2
  YGNodeRef child2 = YGNodeNew();
  YGNodeStyleSetFlexGrow(child2, 1);
  YGNodeStyleSetHeight(child2, 150);
  YGNodeInsertChild(root, child2, 1);

  // 计算布局
  YGNodeCalculateLayout(root, 800, 600, YGDirectionLTR);

  // 主循环
  int running = 1;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = 0;
      }
    }

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);

    render_tree(renderer, root, 0, 0);

    SDL_RenderPresent(renderer);
  }

  // 清理资源
  YGNodeFreeRecursive(root);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}