// canvas setup
const canvas = document.querySelector("canvas");
canvas.width = 640;
canvas.height = 320;
const ctx = canvas.getContext("2d");

ctx.fillStyle = "#F0F0F0"; // background color
ctx.fillRect(0, 0, 640, 320);

// init chip8
let pc = 512;

const memory = [];
for (let i = 0; i < 4096; i++) {
  memory[i] = 0;
}

const v = [];
for (let i = 0; i < 16; i++) {
  v[i] = 0;
}

let ip = 0;

const stack = [];

const display = [];
for (let i = 0; i < 32; i++) {
  display[i] = [];
  for (let j = 0; j < 64; j++) {
    display[i][j] = 0;
  }
}
let display_refresh = true;

let paused = false;
document.addEventListener('keydown', event => {
  if (event.code == "Space") {
    paused = !paused;

    if (!paused) {
      ctx.filter = "none";
      cycle();
    } else {
      ctx.filter = "grayscale(100%)";
      redraw_display();
    }
  }
});

// load font into memory
const font = await(await fetch('font.json')).json();
for (let i = 0; i < font.length; i++) {
  memory[i] = Number(font[i]);
}

// load rom into memory
const rom = new Uint8Array(await(await fetch('particle.ch8')).arrayBuffer());
for (let i = 0; i < rom.length; i++) {
  memory[pc + i] = rom[i];
}

// let's go
cycle();

function cycle() {
  // fetch opcode
  const opcode = memory[pc] << 8 | memory[pc + 1];
  pc += 2;

  // decode opcode
  // XYZW
  const op_x = opcode >> 12;
  const op_y = opcode >> 8 & 0xf; // >> has higher precedence than &
  const op_z = opcode >> 4 & 0xf;
  const op_w = opcode & 0xf;
  const op_zw = opcode & 0xff;
  const op_yzw = opcode & 0xfff;

  // helpful debug message
  let msg = `0x${hex(opcode)}: `;
  let warn = false;

  // execute
  switch (op_x) {
    case 0x0:
      switch (op_zw) {
        case 0xe0:
          display_refresh = true;
          for (let i = 0; i < 32; i++) {
            for (let j = 0; j < 64; j++) {
              display[i][j] = 0;
            }
          }

          msg += `CLS`;
          break;
        case 0xee:
          pc = stack.pop();

          msg += `RET`;
          break;
        default:
          warn = true;
          msg += 'IDK';
          break;
      }
      break;
    case 0x1:
      pc = op_yzw;

      msg += `JP ${hex(op_yzw)}`;
      break;
    case 0x2:
      stack.push(pc);
      pc = op_yzw;

      msg += `CALL ${hex(op_yzw)}`;
      break;
    case 0x3:
      if (v[op_y] == op_zw) {
        pc += 2;
      }

      msg += `SE V${hex(op_y)}, ${hex(op_zw)}`;
      break;
    case 0x4:
      if (v[op_y] != op_zw) {
        pc += 2;
      }

      msg += `SNE V${hex(op_y)}, ${hex(op_zw)}`
      break;
    case 0x5:
      if (v[op_y] == v[op_z]) {
        pc += 2;
      }

      msg += `SE V${hex(op_y)}, V${hex(op_z)}`
      break;
    case 0x6:
      v[op_y] = op_zw;

      msg += `LD V${hex(op_y)}, ${hex(op_zw)}`;
      break;
    case 0x7:
      v[op_y] = v[op_y] + op_zw;
      v[op_y] &= 0xff;

      msg += `ADD V${hex(op_y)}, ${hex(op_zw)}`
      break;
    case 0x8:
      switch (op_w) {
        case 0x0:
          v[op_y] = v[op_z];

          msg += `LD V${hex(op_y)}, V${hex(op_z)}`;
          break;
        case 0x1:
          v[op_y] |= v[op_z];

          msg += `OR V${hex(op_y)}, V${hex(op_z)}`;
          break;
        case 0x2:
          v[op_y] &= v[op_z];

          msg += `AND V${hex(op_y)}, V${hex(op_z)}`;
          break;
        case 0x3:
          v[op_y] ^= v[op_z];

          msg += `XOR V${hex(op_y)}, V${hex(op_z)}`;
          break;
        case 0x4:
          const sum = v[op_y] + v[op_z];
          if (sum > 255) {
            v[0xf] = 1;
          } else {
            v[0xf] = 0;
          }
          v[op_y] = sum & 0xff;

          msg += `ADD V${hex(op_y)}, V${hex(op_z)}`;
          break;
        case 0x5:
          const diff = v[op_y] - v[op_z];
          if (v[op_y] > v[op_z]) {
            v[0xf] = 1;
          } else {
            v[0xf] = 0;
          }
          v[op_y] = diff & 0xff;

          msg += `SUB V${hex(op_y)}, V${hex(op_z)}`;
          break;
        case 0x6:
          v[0xf] = v[op_y] & 1;
          v[op_y] >>= 1;
          v[op_y] &= 0xff;

          msg += `SHR V${hex(op_y)} {, V${hex(op_z)}}`
          break;
        case 0xe:
          v[0xf] = (v[op_y] >> 7) & 1;
          v[op_y] <<= 1;
          v[op_y] &= 0xff;

          msg += `SHL V${hex(op_y)} {, V${hex(op_z)}}`
          break;
        default:
          warn = true;
          msg += 'IDK';
          break;
      }
      break;
    case 0x9:
      if (v[op_y] != v[op_z]) {
        pc += 2;
      }

      msg += `SNE V${hex(op_y)}, V${hex(op_z)}`
      break;
    case 0xa:
      ip = op_yzw;

      msg += `LD I, ${hex(op_yzw)}`;
      break;
    case 0xc:
      v[op_y] = Math.floor(Math.random() * 255) & op_zw;

      msg += `RND V${hex(op_y)}, ${hex(op_zw)}`
      break;
    case 0xd:
      display_refresh = true;

      for (let i = 0; i < op_w; i++) {
        let base_x = v[op_y];
        let base_y = v[op_z] + i;

        let sprite = memory[ip + i];
        for (let j = 7; j >= 0; j--) {
          let x = (base_x + (7 - j)) % 64;
          let y = (base_y) % 32;
          let pixel = display[y][x] ^ ((sprite >> j) & 1);

          v[0xf] = display[y][x] && !pixel;
          display[y][x] = pixel;
        }
      }

      msg += `DRW V${hex(op_y)}, V${hex(op_z)}, ${hex(op_w)}`;
      break;
    case 0xf:
      switch (op_zw) {
        case 0x1e:
          ip += v[op_y];

          msg += `ADD I(${hex(ip)}), V[${hex(op_y)}]`
          break;
        case 0x29:
          ip = v[op_y] * 5;

          msg += `LD F, V${hex(op_y)}`
          break;
        case 0x33:
          memory[ip] = Math.trunc(v[op_y] / 100);
          memory[ip + 1] = Math.trunc(v[op_y] / 10) % 10;
          memory[ip + 2] = v[op_y] % 10;

          msg += `LD B, V${hex(op_y)}`;
          break;
        case 0x55:
          for (let i = 0; i <= op_y; i++) {
            memory[ip + i] = v[i];
          }

          msg += `LD MEM[${hex(ip)}], V${hex(op_y)}`;
          break;
        case 0x65:
          for (let i = 0; i <= op_y; i++) {
            v[i] = memory[ip + i];
          }

          msg += `LD V${hex(op_y)}, MEM[${hex(ip)}]`;
          break;
        default:
          warn = true;
          msg += 'IDK';
          break;
      }
      break;
    default:
      warn = true;
      msg += 'IDK';
      break;
  }

  if (display_refresh) {
    display_refresh = false;
    redraw_display();
  }

  if (warn) {
    console.warn(msg);
  } else {
    console.log(msg);
  }

  if (!paused) {
    setTimeout(cycle, 1000 / 500); // 500 Hz
  }
}

function redraw_display() {
  for (let i = 0; i < 32; i++) {
    for (let j = 0; j < 64; j++) {
      if (display[i][j]) {
        ctx.fillStyle = "#F15025";
      } else {
        ctx.fillStyle = "#F0F0F0";
      }
      ctx.fillRect(j * 10, i * 10, 10, 10);
    }
  }
}

// helper functions
function hex(n) {
  return n.toString(16);
}
