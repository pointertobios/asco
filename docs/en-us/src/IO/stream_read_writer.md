# Stream Reader/Writer (`stream_read_writer`)

`stream_read_writer<T>` directly derives from [`stream_reader<T>`](./stream_reader.md) and [`stream_writer<T>`](./stream_writer.md). It is intended for duplex (bidirectional) underlying stream objects.

The data you read and the data you write are not required to be related; semantics are entirely determined by the underlying I/O object's behavior.
