// Phase 1 protocol verification: tracks/mixer/seek/peaks/export.
import net from 'net'

const sleep = (ms) => new Promise(r => setTimeout(r, ms))
const sock = net.createConnection({ port: 7177, host: '127.0.0.1' })
const send = (obj) => sock.write(JSON.stringify(obj) + '\n')

let buf = ''
let lastTracks = null
let lastPeaks = null
let exported = null

sock.on('data', (chunk) => {
  buf += chunk.toString()
  let nl
  while ((nl = buf.indexOf('\n')) >= 0) {
    const line = buf.slice(0, nl); buf = buf.slice(nl + 1)
    if (!line.trim()) continue
    const ev = JSON.parse(line)
    if (ev.event === 'tracks') lastTracks = ev
    else if (ev.event === 'peaks') lastPeaks = ev
    else if (ev.event === 'exported') exported = ev
    else if (ev.event === 'hello') console.log('HELLO', ev.device, ev.sampleRate, ev.bufferSize)
  }
})

const summary = () => lastTracks.tracks.map((t, i) =>
  `  [${i}] ${t.name} armed=${t.armed} mute=${t.mute} solo=${t.solo} vol=${t.volumeDb?.toFixed(1)} pan=${t.pan?.toFixed(2)} clips=${t.clips.map(c => `${c.name}(${c.length.toFixed(1)}s,takes:${c.takes})`).join(',') || '-'}`
).join('\n')

sock.on('connect', async () => {
  for (let i = 0; i < 40 && !lastTracks; i++) await sleep(250)
  if (!lastTracks) { console.error('no tracks event within 10s'); process.exit(1) }
  console.log('initial tracks:\n' + summary())

  send({ cmd: 'addTrack' }); await sleep(300)
  send({ cmd: 'arm', track: 1, on: true }); await sleep(300)
  console.log('after addTrack + arm track 2:\n' + summary())

  send({ cmd: 'volume', track: 0, db: -6 })
  send({ cmd: 'pan', track: 0, value: -0.4 })
  send({ cmd: 'mute', track: 0, on: true }); await sleep(300)
  console.log('after vol/pan/mute on track 1:\n' + summary())
  send({ cmd: 'mute', track: 0, on: false })

  console.log('-> record 2.5s onto track 2')
  send({ cmd: 'record' }); await sleep(2500)
  send({ cmd: 'stop' }); await sleep(800)
  console.log('after record:\n' + summary())

  const t = lastTracks.tracks[1]
  if (t.clips.length > 0) {
    send({ cmd: 'peaks', track: 1, clip: 0 }); await sleep(800)
    console.log('peaks:', lastPeaks ? `${lastPeaks.data.length} buckets @${lastPeaks.peaksPerSecond}/s, max=${Math.max(...lastPeaks.data)}` : 'NONE')
  }

  send({ cmd: 'seek', seconds: 1.0 }); await sleep(300)

  console.log('-> export wav 24-bit')
  send({ cmd: 'export', format: 'wav', bitDepth: 24 })
  for (let i = 0; i < 40 && !exported; i++) await sleep(500)
  console.log('exported:', JSON.stringify(exported))

  send({ cmd: 'quit' }); await sleep(300)
  process.exit(0)
})

sock.on('error', (e) => { console.error('socket error:', e.message); process.exit(1) })
