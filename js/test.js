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
