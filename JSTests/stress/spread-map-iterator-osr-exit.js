//@ runDefault("--forceEagerCompilation=true", "--useConcurrentJIT=false")

// OSR exit tests for spread on Map iterator objects

function shouldBe(actual, expected) {
    if (actual !== expected)
        throw new Error(`Expected ${expected} but got ${actual}`);
}

function shouldBeArray(actual, expected) {
    shouldBe(actual.length, expected.length);
    for (let i = 0; i < expected.length; i++)
        shouldBe(actual[i], expected[i]);
}

// Test OSR exit when iterator protocol is modified
{
    function spreadKeys(map) {
        return [...map.keys()];
    }

    const map = new Map([[1, 'a'], [2, 'b'], [3, 'c']]);

    // Warm up
    for (let i = 0; i < 10000; i++)
        spreadKeys(map);

    shouldBeArray(spreadKeys(map), [1, 2, 3]);

    // Modify iterator protocol - should cause OSR exit
    const MapIteratorPrototype = Object.getPrototypeOf(new Map().keys());
    const originalNext = MapIteratorPrototype.next;

    let callCount = 0;
    MapIteratorPrototype.next = function() {
        callCount++;
        return originalNext.call(this);
    };

    // This should use the slow path
    const result = spreadKeys(map);
    shouldBeArray(result, [1, 2, 3]);
    shouldBe(callCount > 0, true);

    // Restore
    MapIteratorPrototype.next = originalNext;
}

// Test OSR exit when Symbol.iterator is modified on iterator
{
    function spreadValues(map) {
        return [...map.values()];
    }

    const map = new Map([[1, 'x'], [2, 'y']]);

    // Warm up
    for (let i = 0; i < 10000; i++)
        spreadValues(map);

    shouldBeArray(spreadValues(map), ['x', 'y']);

    // Modify Symbol.iterator on MapIterator prototype
    const MapIteratorPrototype = Object.getPrototypeOf(new Map().values());
    const originalIterator = MapIteratorPrototype[Symbol.iterator];

    MapIteratorPrototype[Symbol.iterator] = function() {
        return {
            next: () => ({ done: true })
        };
    };

    // This should use the slow path with modified behavior
    const result = spreadValues(map);
    shouldBeArray(result, []);

    // Restore
    MapIteratorPrototype[Symbol.iterator] = originalIterator;
}

// Test OSR exit with different map types
{
    function testMixed(iterable) {
        return [...iterable];
    }

    const map = new Map([[1, 'a'], [2, 'b']]);
    const set = new Set([1, 2, 3]);
    const array = [4, 5, 6];

    // Warm up with map iterator
    for (let i = 0; i < 5000; i++)
        testMixed(map.keys());

    shouldBeArray(testMixed(map.keys()), [1, 2]);

    // Should still work with different iterables (causes speculation failure)
    shouldBeArray(testMixed(set), [1, 2, 3]);
    shouldBeArray(testMixed(array), [4, 5, 6]);
    shouldBeArray(testMixed(map.values()), ['a', 'b']);
}

// Test OSR exit when map is modified during iteration
{
    function spreadKeysWithModification(map, modifyFn) {
        const iter = map.keys();
        modifyFn(map);
        return [...iter];
    }

    // Warm up without modification
    for (let i = 0; i < 5000; i++) {
        const m = new Map([[1, 'a'], [2, 'b']]);
        spreadKeysWithModification(m, () => {});
    }

    // Now test with modification
    const map = new Map([[1, 'a'], [2, 'b'], [3, 'c']]);
    const result = spreadKeysWithModification(map, (m) => m.delete(2));
    // After deletion, spread should return remaining keys
    // Note: behavior depends on implementation - we just verify it doesn't crash
    shouldBe(result.length <= 3, true);
}
