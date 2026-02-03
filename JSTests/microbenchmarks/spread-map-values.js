// Benchmark for spread on Map.values()

function benchmarkSpreadMapValues() {
    const map = new Map();
    for (let i = 0; i < 100; i++)
        map.set(i, i * 2);

    let sum = 0;
    for (let i = 0; i < 10000; i++) {
        const arr = [...map.values()];
        sum += arr.length;
    }
    return sum;
}

benchmarkSpreadMapValues();
