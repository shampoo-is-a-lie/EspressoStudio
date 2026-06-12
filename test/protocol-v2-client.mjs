// Phase 2 protocol verification: clip editing, fades, takes, undo/redo.
import net from 'net'

const sleep = (ms) => new Promise(r => setTimeout(r, ms))
const sock = net.createConnection({ port: 7177, host: '127.0.0.1' })
const send = (obj) => sock.write(JSON.stringify(obj) + '\n')

let buf = ''
let lastTracks = null
sock.on('data', (chunk) => {
  buf += chunk.toString()
  let nl
  while ((nl = buf.indexOf('\n')) >= 0) {
    const line = buf.slice(0, nl); buf = buf.slice(nl + 1)
    if (!line.trim()) continue
    const ev = JSON.parse(line)
    if (ev.event === 'tracks') lastTracks = ev
  }
})

const clips = (ti) => (lastTracks.tracks[ti]?.clips ?? [])
  .map(c => `${c.name}[${c.start.toFixed(2)}→${(c.start + c.length).toFixed(2)} off=${c.offset.toFixed(2)} fi=${c.fadeIn.toFixed(2)} fo=${c.fadeOut.toFixed(2)}]`)
  .join(' ')

sock.on('connect', async () => {
  for (let i = 0; i < 40 && !lastTracks; i++) await sleep(250)

  // ensure there's a clip to edit: record 3s onto track 1
  send({ cmd: 'rewind' }); await sleep(200)
  send({ cmd: 'record' }); await sleep(3000)
  send({ cmd: 'stop' }); await sleep(1000)
  console.log('recorded   :', clips(0))

  send({ cmd: 'moveClip', track: 0, clip: 0, start: 2.0 }); await sleep(400)
  console.log('moved→2.0  :', clips(0))

  send({ cmd: 'trimClip', track: 0, clip: 0, edge: 'start', to: 2.5 }); await sleep(400)
  console.log('trimL→2.5  :', clips(0))

  send({ cmd: 'trimClip', track: 0, clip: 0, edge: 'end', to: 4.0 }); await sleep(400)
  console.log('trimR→4.0  :', clips(0))

  send({ cmd: 'splitClip', track: 0, clip: 0, at: 3.2 }); await sleep(400)
  console.log('split@3.2  :', clips(0))

  send({ cmd: 'setFades', track: 0, clip: 0, fadeIn: 0.2, fadeOut: 0.1 }); await sleep(400)
  console.log('fades      :', clips(0))

  send({ cmd: 'deleteClip', track: 0, clip: 1 }); await sleep(400)
  console.log('deleted #2 :', clips(0))

  send({ cmd: 'undo' }); await sleep(400)
  console.log('undo       :', clips(0))

  send({ cmd: 'redo' }); await sleep(400)
  console.log('redo       :', clips(0))

  process.exit(0)
})

sock.on('error', (e) => { console.error('socket error:', e.message); process.exit(1) })
