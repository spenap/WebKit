const a = [undefined];
a.toString = ()=>{};

function foo() {
    for (let x in a) {
      x in a;
      +x;
    }
}

for (let i=0; i<testLoopCount; i++) {
  foo();
}
