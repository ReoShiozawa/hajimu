'use strict';

// LSP/DAP共通フレーミング。Content-LengthはUTF-8バイト数で扱う。
function transport(input, output, receive) {
  let buffer = Buffer.alloc(0);
  const send = (message) => {
    const body = Buffer.from(JSON.stringify(message));
    output.write(`Content-Length: ${body.length}\r\n\r\n`);
    output.write(body);
  };
  input.on('data', (chunk) => {
    buffer = Buffer.concat([buffer, chunk]);
    try {
      for (;;) {
        const end = buffer.indexOf('\r\n\r\n');
        if (end < 0) {
          if (buffer.length > 8192) throw new Error('header too large');
          return;
        }
        const header = buffer.subarray(0, end).toString('ascii');
        const lengths = [...header.matchAll(/^Content-Length:\s*(\d+)\s*$/gim)];
        if (lengths.length !== 1) throw new Error('invalid Content-Length');
        const size = Number(lengths[0][1]);
        if (!Number.isSafeInteger(size) || size < 0 || size > 16 * 1024 * 1024) throw new Error('body too large');
        if (buffer.length < end + 4 + size) return;
        const body = buffer.subarray(end + 4, end + 4 + size);
        buffer = buffer.subarray(end + 4 + size);
        Promise.resolve(receive(JSON.parse(body.toString('utf8')))).catch((error) => {
          process.stderr.write(`${error.message}\n`);
        });
      }
    } catch (error) {
      process.stderr.write(`protocol error: ${error.message}\n`);
      input.destroy();
      process.exitCode = 1;
    }
  });
  return send;
}
module.exports = { transport };
