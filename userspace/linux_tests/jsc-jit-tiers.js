// Run with the real jsc shell and --reportCompileTimes=true. Repeat with
// DFG/FTL disabled independently, then with all build-available tiers enabled.
// No hardcoded allocator, policy hint or benchmark-only engine patch.
let finalTierObserved = false;
function hot(a, bias) {
    if (isFinalTier()) finalTierObserved = true;
    let sum = bias;
    for (let i=0;i<a.length;i++) sum = (sum + ((a[i] * 17) ^ i)) | 0;
    return sum;
}
let a = new Int32Array(256);
for (let i=0;i<a.length;i++) a[i] = i*3+1;
let reference=0;
for (let i=0;i<a.length;i++) reference=(reference + ((a[i]*17)^i))|0;
let checksum=0;
let start=Date.now();
// A finite short loop may exit before the asynchronous optimizer publishes
// FTL. Keep exercising real code across successive normal tiering phases;
// do not force compilation, change thresholds, sleep or fake completion.
for (let i=0;i<600000;i++) {
    const result=hot(a,i&7);
    if (result !== reference+(i&7)) throw new Error("hot code result diverged at "+i);
    checksum=(checksum+result)|0;
}
// Polymorphic ICs and property repatching; code must remain writable only via
// the native RW/NX alias after initial linking/publication of the RX address.
function property(o) { return o.value + 3; }
for (let i=0;i<20000;i++) {
    let object=i&1 ? {value:i,extra:1} : {value:i};
    if (property(object)!==i+3) throw new Error("IC/repatch mismatch");
}
for (let i=0;i<20000;i++) {
    if (!/^(?:abc|xyz)+[0-9]+$/.test("abcxyz123")) throw new Error("RegExp JIT mismatch");
}
// An async compiler may legitimately publish after the finite hot loop has
// ended. Continue useful, oracle-checked execution until the final tier is
// actually entered. This neither forces compilation nor changes thresholds.
// A missing/broken tier still fails after the bounded test deadline.
let extraHotCalls = 0;
if (arguments[0] === "concurrent") {
    const deadline = Date.now() + 15000;
    while (!finalTierObserved) {
        if (Date.now() >= deadline) throw new Error("Concurrent final tier never executed");
        for (let i=0;i<1000;i++) {
            const result=hot(a,i&7);
            if (result !== reference+(i&7)) throw new Error("Concurrent hot code diverged");
            checksum=(checksum+result)|0;
            extraHotCalls++;
        }
    }
}
print("JSC JIT tiers: checksum="+checksum+" elapsed_ms="+(Date.now()-start)
    +" hot_DFG_compiles="+numberOfDFGCompiles(hot)+" final_tier_executed="+finalTierObserved
    +" extra_hot_calls="+extraHotCalls);
