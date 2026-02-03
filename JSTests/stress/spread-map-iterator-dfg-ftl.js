//@ runDefault("--forceEagerCompilation=true", "--useConcurrentJIT=false")

// DFG/FTL optimization tests for spread on Map iterator objects

function shouldBe(actual, expected) {
    if (actual !== expected)
        throw new Error(`Expected ${expected} but got ${actual}`);
}

function shouldBeArray(actual, expected) {
    shouldBe(actual.length, expected.length);
    for (let i = 0; i < expected.length; i++)
        shouldBe(actual[i], expected[i]);
}

// Test that spread optimization works in DFG/FTL
function testSpreadKeys(map) {
    return [...map.keys()];
}

function testSpreadValues(map) {
    return [...map.values()];
}

const map = new Map([[1, 'a'], [2, 'b'], [3, 'c']]);

// Warm up to trigger DFG/FTL compilation
for (let i = 0; i < 10000; i++) {
    testSpreadKeys(map);
    testSpreadValues(map);
}

// Verify correctness after optimization
shouldBeArray(testSpreadKeys(map), [1, 2, 3]);
shouldBeArray(testSpreadValues(map), ['a', 'b', 'c']);

// Test with different maps to ensure polymorphic behavior
const map2 = new Map([['x', 10], ['y', 20]]);
shouldBeArray(testSpreadKeys(map2), ['x', 'y']);
shouldBeArray(testSpreadValues(map2), [10, 20]);

// Test empty map
const emptyMap = new Map();
shouldBeArray(testSpreadKeys(emptyMap), []);
shouldBeArray(testSpreadValues(emptyMap), []);

// Test that iterator consumption is correct in optimized code
function testPartialIteratorKeys(map) {
    const iter = map.keys();
    iter.next();
    return [...iter];
}

function testPartialIteratorValues(map) {
    const iter = map.values();
    iter.next();
    return [...iter];
}

for (let i = 0; i < 10000; i++) {
    testPartialIteratorKeys(map);
    testPartialIteratorValues(map);
}

shouldBeArray(testPartialIteratorKeys(map), [2, 3]);
shouldBeArray(testPartialIteratorValues(map), ['b', 'c']);

// Test inline caching with consistent structure
function testConsistentStructure() {
    const m = new Map();
    for (let i = 0; i < 5; i++)
        m.set(i, i * 10);
    return [...m.keys()].reduce((a, b) => a + b, 0);
}

for (let i = 0; i < 10000; i++)
    testConsistentStructure();

shouldBe(testConsistentStructure(), 0 + 1 + 2 + 3 + 4);
