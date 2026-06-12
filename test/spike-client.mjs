// Phase 0 verification client: drives the engine over TCP like the app would.
import net from 'net'

const sleep = (ms) => new Promise(r => setTimeout(r, ms))
const sock = net.createConnection({ port: 7177, host: '127.0.0.1' })
const send = (cmd) => sock.write(JSON.stringify({ cmd }) + '\n')

let buf = ''
const states = []
sock.on('data', (chunk) => {
  buf += chunk.toString()
  let nl
  while ((nl = buf.indexOf('\n')) >= 0) {
    const line = buf.slice(0, nl); buf = buf.slice(nl + 1)
    if (!line.trim()) continue
    const ev = JSON.parse(line)
    if (ev.event === 'hello') console.log('HELLO', JSON.stringify(ev))
    else states.push(ev)
  }
})

sock.on('connect', async () => {
  console.log('connected')
  await sleep(500)

  console.log('-> record')
  send('record')
  await sleep(3000)
  console.log('   during record:', JSON.stringify(states.at(-1)))

  console.log('-> stop')
  send('stop')
  await sleep(800)

  console.log('-> play')
  send('play')
  await sleep(2000)
  console.log('   during play:', JSON.stringify(states.at(-1)))
  const peak = Math.max(...states.slice(-50).map(s => Math.max(s.levelL, s.levelR)))
  console.log('   peak level during playback:', peak.toFixed(4))

  console.log('-> stop, quit')
  send('stop')
  await sleep(500)
  console.log('   after stop:', JSON.stringify(states.at(-1)))
  send('quit')
  await sleep(500)
  sock.destroy()
  console.log('total state events received:', states.length)
  process.exit(0)
})

sock.on('error', (e) => { console.error('socket error:', e.message); process.exit(1) })
