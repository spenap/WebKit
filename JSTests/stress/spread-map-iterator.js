// Basic functionality tests for spread on Map iterator objects

function shouldBe(actual, expected) {
    if (actual !== expected)
        throw new Error(`Expected ${expected} but got ${actual}`);
}

function shouldBeArray(actual, expected) {
    shouldBe(actual.length, expected.length);
    for (let i = 0; i < expected.length; i++)
        shouldBe(actual[i], expected[i]);
}

// Test basic spread on map.keys()
{
    const map = new Map([[1, 'a'], [2, 'b'], [3, 'c']]);
    const keys = [...map.keys()];
    shouldBeArray(keys, [1, 2, 3]);
}

// Test basic spread on map.values()
{
    const map = new Map([[1, 'a'], [2, 'b'], [3, 'c']]);
    const values = [...map.values()];
    shouldBeArray(values, ['a', 'b', 'c']);
}

// Test empty map
{
    const map = new Map();
    const keys = [...map.keys()];
    const values = [...map.values()];
    shouldBeArray(keys, []);
    shouldBeArray(values, []);
}

// Test single element
{
    const map = new Map([[42, 'answer']]);
    shouldBeArray([...map.keys()], [42]);
    shouldBeArray([...map.values()], ['answer']);
}

// Test various key/value types
{
    const obj = {};
    const sym = Symbol('test');
    const map = new Map([
        ['string', 1],
        [42, 2],
        [obj, 3],
        [sym, 4],
        [null, 5],
        [undefined, 6]
    ]);

    const keys = [...map.keys()];
    shouldBe(keys.length, 6);
    shouldBe(keys[0], 'string');
    shouldBe(keys[1], 42);
    shouldBe(keys[2], obj);
    shouldBe(keys[3], sym);
    shouldBe(keys[4], null);
    shouldBe(keys[5], undefined);

    const values = [...map.values()];
    shouldBeArray(values, [1, 2, 3, 4, 5, 6]);
}

// Test that spread consumes iterator
{
    const map = new Map([[1, 'a'], [2, 'b'], [3, 'c']]);
    const iter = map.keys();
    iter.next(); // consume first element
    const remaining = [...iter];
    shouldBeArray(remaining, [2, 3]);
}

// Test that spread consumes iterator (values)
{
    const map = new Map([[1, 'a'], [2, 'b'], [3, 'c']]);
    const iter = map.values();
    iter.next(); // consume first element
    const remaining = [...iter];
    shouldBeArray(remaining, ['b', 'c']);
}

// Test closed iterator
{
    const map = new Map([[1, 'a'], [2, 'b']]);
    const iter = map.keys();
    iter.next();
    iter.next();
    iter.next(); // close iterator
    const result = [...iter];
    shouldBeArray(result, []);
}

// Test large map
{
    const map = new Map();
    for (let i = 0; i < 1000; i++)
        map.set(i, i * 2);

    const keys = [...map.keys()];
    const values = [...map.values()];

    shouldBe(keys.length, 1000);
    shouldBe(values.length, 1000);
    shouldBe(keys[500], 500);
    shouldBe(values[500], 1000);
}

// Test that original map is not modified
{
    const map = new Map([[1, 'a'], [2, 'b']]);
    const keys = [...map.keys()];
    keys.push(3);
    shouldBe(map.size, 2);
    shouldBe(keys.length, 3);
}

// Test insertion order is preserved
{
    const map = new Map();
    map.set('z', 1);
    map.set('a', 2);
    map.set('m', 3);

    shouldBeArray([...map.keys()], ['z', 'a', 'm']);
    shouldBeArray([...map.values()], [1, 2, 3]);
}

// Test re-insertion maintains order
{
    const map = new Map([[1, 'a'], [2, 'b'], [3, 'c']]);
    map.set(1, 'x'); // re-insert, should not change order
    shouldBeArray([...map.keys()], [1, 2, 3]);
    shouldBeArray([...map.values()], ['x', 'b', 'c']);
}

// Test delete and spread
{
    const map = new Map([[1, 'a'], [2, 'b'], [3, 'c']]);
    map.delete(2);
    shouldBeArray([...map.keys()], [1, 3]);
    shouldBeArray([...map.values()], ['a', 'c']);
}
