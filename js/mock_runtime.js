// A simple runtime that doesn't involve typescript or protobufs to test
// libdeno. Invoked by mock_runtime_test.cc

const global = this;

function assert(cond) {
  if (!cond) throw Error("mock_runtime.js assert failed");
}

global.CanCallFunction = () => {
  deno.print("Hello world from foo");
  return "foo";
};

// This object is created to test snapshotting.
// See DeserializeInternalFieldsCallback and SerializeInternalFieldsCallback.
const snapshotted = new Uint8Array([1, 3, 3, 7]);

global.TypedArraySnapshots = () => {
  assert(snapshotted[0] === 1);
  assert(snapshotted[1] === 3);
  assert(snapshotted[2] === 3);
  assert(snapshotted[3] === 7);
};

global.SendSuccess = () => {
  deno.recv(msg => {
    deno.print("SendSuccess: ok");
  });
};

global.SendByteLength = () => {
  deno.recv(msg => {
    assert(msg instanceof ArrayBuffer);
    assert(msg.byteLength === 3);
  });
};

global.RecvReturnEmpty = () => {
  const m1 = new Uint8Array("abc".split("").map(c => c.charCodeAt(0)));
  const m2 = m1.slice();
  const r1 = deno.send(m1);
  assert(r1 == null);
  const r2 = deno.send(m2);
  assert(r2 == null);
};

global.RecvReturnBar = () => {
  const m = new Uint8Array("abc".split("").map(c => c.charCodeAt(0)));
  const r = deno.send(m);
  assert(r instanceof Uint8Array);
  assert(r.byteLength === 3);
  const rstr = String.fromCharCode(...r);
  assert(rstr === "bar");
};

global.DoubleRecvFails = () => {
  // deno.recv is an internal function and should only be called once from the
  // runtime.
  deno.recv((channel, msg) => assert(false));
  deno.recv((channel, msg) => assert(false));
};

// The following join has caused SnapshotBug to segfault when using kKeep.
[].join("");

global.SnapshotBug = () => {
  assert("1,2,3" === String([1, 2, 3]));
};

global.ErrorHandling = () => {
  global.onerror = (message, source, line, col, error) => {
    deno.print(`line ${line} col ${col}`);
    assert("ReferenceError: notdefined is not defined" === message);
    assert(source === "helloworld.js");
    assert(line === 3);
    assert(col === 1);
    assert(error instanceof Error);
    deno.send(new Uint8Array([42]));
  };
  eval("\n\n notdefined()\n//# sourceURL=helloworld.js");
};
