//@ runDefault("--forceEagerCompilation=true", "--useConcurrentJIT=false")

// Having-a-bad-time tests for spread on Map iterator objects

function shouldBe(actual, expected) {
    if (actual !== expected)
        throw new Error(`Expected ${expected} but got ${actual}`);
}

function shouldBeArray(actual, expected) {
    shouldBe(actual.length, expected.length);
    for (let i = 0; i < expected.length; i++)
        shouldBe(actual[i], expected[i]);
}

// Test that spread works correctly even in having-a-bad-time mode
{
    function spreadKeys(map) {
        return [...map.keys()];
    }

    function spreadValues(map) {
        return [...map.values()];
    }

    const map = new Map([[1, 'a'], [2, 'b'], [3, 'c']]);

    // Warm up
    for (let i = 0; i < 10000; i++) {
        spreadKeys(map);
        spreadValues(map);
    }

    shouldBeArray(spreadKeys(map), [1, 2, 3]);
    shouldBeArray(spreadValues(map), ['a', 'b', 'c']);

    // Trigger having-a-bad-time by modifying Array prototype
    Array.prototype[10] = 'bad';

    // Spread should still work correctly
    const keys = spreadKeys(map);
    const values = spreadValues(map);

    shouldBeArray(keys, [1, 2, 3]);
    shouldBeArray(values, ['a', 'b', 'c']);

    // Verify the array doesn't have prototype pollution
    shouldBe(keys.hasOwnProperty(10), false);
    shouldBe(values.hasOwnProperty(10), false);

    // Clean up
    delete Array.prototype[10];
}

// Test with Object.prototype modification
{
    function spreadKeys(map) {
        return [...map.keys()];
    }

    const map = new Map([['x', 1], ['y', 2]]);

    // Warm up
    for (let i = 0; i < 5000; i++)
        spreadKeys(map);

    // Modify Object.prototype
    Object.prototype.badProperty = 'evil';

    const result = spreadKeys(map);
    shouldBeArray(result, ['x', 'y']);

    // Clean up
    delete Object.prototype.badProperty;
}

// Test with Array constructor replacement (should not affect Map spread)
{
    function spreadValues(map) {
        return [...map.values()];
    }

    const map = new Map([[1, 100], [2, 200]]);

    // Warm up
    for (let i = 0; i < 5000; i++)
        spreadValues(map);

    // Note: We can't actually replace Array constructor globally,
    // but we can verify the spread creates proper arrays
    const result = spreadValues(map);
    shouldBe(Array.isArray(result), true);
    shouldBe(result.constructor, Array);
    shouldBeArray(result, [100, 200]);
}

// Test with indexed property on Array.prototype (having-a-bad-time trigger)
{
    function testSpread(map) {
        const keys = [...map.keys()];
        const values = [...map.values()];
        return { keys, values };
    }

    const map = new Map([[0, 'zero'], [1, 'one'], [2, 'two']]);

    // Warm up
    for (let i = 0; i < 5000; i++)
        testSpread(map);

    // Add indexed property to Array.prototype
    Array.prototype[1] = 'prototype-value';

    const result = testSpread(map);

    // The spread result should have its own property at index 1
    shouldBe(result.keys.hasOwnProperty(1), true);
    shouldBe(result.keys[1], 1);
    shouldBe(result.values.hasOwnProperty(1), true);
    shouldBe(result.values[1], 'one');

    // Clean up
    delete Array.prototype[1];
}

// Test combination of modifications
{
    function spreadTest(map) {
        return [...map.keys(), ...map.values()];
    }

    const map = new Map([['a', 1], ['b', 2]]);

    // Warm up
    for (let i = 0; i < 5000; i++)
        spreadTest(map);

    // Multiple modifications
    Array.prototype[5] = 'bad5';
    Object.prototype.weird = 'weird';

    const result = spreadTest(map);
    shouldBeArray(result, ['a', 'b', 1, 2]);

    // Clean up
    delete Array.prototype[5];
    delete Object.prototype.weird;
}
