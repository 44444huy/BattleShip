import argparse
import asyncio
import time


async def connect_user(user_id, host, port, ramp_ms, timeout):
    await asyncio.sleep(user_id * ramp_ms / 1000)

    reader, writer = await asyncio.wait_for(
        asyncio.open_connection(host, port),
        timeout
    )

    greeting = await asyncio.wait_for(reader.readline(), timeout)
    greeting = greeting.decode().strip()

    if greeting != "CONNECTED":
        writer.close()
        await writer.wait_closed()
        raise RuntimeError(f"user {user_id}: received {greeting!r}")

    return reader, writer


async def ping_user(user_id, reader, writer, timeout):
    started = time.perf_counter()

    writer.write(b"PING\n")
    await writer.drain()

    response = await asyncio.wait_for(reader.readline(), timeout)
    response = response.decode().strip()

    if response != "PONG":
        raise RuntimeError(f"user {user_id}: received {response!r}")

    return (time.perf_counter() - started) * 1000


def percentile(values, percentage):
    values = sorted(values)
    index = int((len(values) - 1) * percentage)
    return values[index]


async def run(args):
    print(f"Connecting {args.users} users...")

    results = await asyncio.gather(
        *[
            connect_user(
                user_id,
                args.host,
                args.port,
                args.ramp_ms,
                args.timeout
            )
            for user_id in range(args.users)
        ],
        return_exceptions=True
    )

    clients = []
    failures = []

    for user_id, result in enumerate(results):
        if isinstance(result, Exception):
            failures.append(f"user {user_id}: {result}")
        else:
            clients.append(result)

    print(f"Connected: {len(clients)}/{args.users}")

    latencies = []
    started = time.perf_counter()

    for _ in range(args.rounds):
        round_results = await asyncio.gather(
            *[
                ping_user(user_id, reader, writer, args.timeout)
                for user_id, (reader, writer) in enumerate(clients)
            ],
            return_exceptions=True
        )

        for result in round_results:
            if isinstance(result, Exception):
                failures.append(str(result))
            else:
                latencies.append(result)

    duration = time.perf_counter() - started

    # The 101st connection should be rejected when all 100 slots are occupied.
    overflow_result = "not tested"
    if len(clients) == 100:
        reader, writer = await asyncio.open_connection(args.host, args.port)
        overflow_result = (
            await asyncio.wait_for(reader.readline(), args.timeout)
        ).decode().strip()
        writer.close()
        await writer.wait_closed()

    for _, writer in clients:
        writer.write(b"QUIT\n")

    await asyncio.gather(
        *[writer.drain() for _, writer in clients],
        return_exceptions=True
    )

    for _, writer in clients:
        writer.close()

    await asyncio.gather(
        *[writer.wait_closed() for _, writer in clients],
        return_exceptions=True
    )

    print(f"PING requests: {len(latencies)}")
    print(f"Duration:      {duration:.2f} seconds")

    if duration > 0:
        print(f"Throughput:    {len(latencies) / duration:.1f} requests/sec")

    if latencies:
        print(f"Minimum:       {min(latencies):.2f} ms")
        print(f"Average:       {sum(latencies) / len(latencies):.2f} ms")
        print(f"P95:           {percentile(latencies, 0.95):.2f} ms")
        print(f"P99:           {percentile(latencies, 0.99):.2f} ms")
        print(f"Maximum:       {max(latencies):.2f} ms")

    print(f"101st client:  {overflow_result}")

    if failures:
        print(f"Failures:      {len(failures)}")
        for failure in failures[:10]:
            print(f"  {failure}")
        raise SystemExit(1)

    if len(clients) != args.users:
        raise SystemExit(1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--users", type=int, default=100)
    parser.add_argument("--rounds", type=int, default=20)
    parser.add_argument("--ramp-ms", type=int, default=20)
    parser.add_argument("--timeout", type=float, default=5)
    asyncio.run(run(parser.parse_args()))
