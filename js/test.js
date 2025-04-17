const colorList = ["#FF0000", "#00FF00", "#0000FF"];
const genColor = () => {
  return colorList[((Math.random() * 1000) >>> 0) % colorList.length];
};

setTimeout(() => {
  // 创建节点
  const parent = createNode("NODE");
  const child1 = createNode("NODE");
  const child2 = createNode("NODE");
  setAttribute(child2, "margin", 5);

  let a = () => {
    print("click this node -----------");
  };

  // 添加子节点
  appendChild(parent, child1);
  appendChild(parent, child2);
  setAttribute(child1, "backgroundColor", genColor());
  setAttribute(child2, "backgroundColor", genColor());
  appendChild(document, parent);
  addEventListener(parent, "click", a);

  let lastNode;
  setInterval(() => {
    // if (lastNode) {
    //   removeChild(parent, lastNode);
    // }
    lastNode = createNode("NODE");
    appendChild(parent, lastNode);
    addEventListener(lastNode, "click", a);
    // dispatchEvent(parent, "click");
    // removeEventListener(parent, "click", a);
    setAttribute(lastNode, "backgroundColor", genColor());
  }, 2000);
}, 500);

let a = setTimeout(() => {
  print("Hello from setTimeout after 2 second");
}, 2000);

setTimeout(() => {
  print("Hello from setTimeout after 1 second");
  Promise.resolve().then(() => {
    print("Running Promise.resolve after 1 second");
  });
  clearTimeout(a);
}, 1000);

Promise.resolve().then(() => {
  print("after running Promise.resolve");
});

let count = 0;
const intervalId = setInterval(() => {
  print(`Interval count: ${++count}`);
  if (count >= 3) {
    clearInterval(intervalId);
    print("Stopped interval");
  }
}, 500);

setTimeout(() => {
  print("Timeout after 1 second");
}, 1000);

print("Main script executed");
