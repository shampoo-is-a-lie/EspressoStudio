import net from 'net'
import { spawn } from 'child_process'
import { EventEmitter } from 'events'

const ENGINE_PORT = 7177
const ENGINE_HOST = '127.0.0.1'

// The engine binary is built inside the espresso-dev toolbox container,
// so by default we launch it through `toolbox run`. Override with
// ESPRESSO_ENGINE_CMD to run a host-native binary directly.
const DEFAULT_ENGINE_CMD = process.env.ESPRESSO_ENGINE_CMD
  || 'bash scripts/engine-ctl.sh start'

export class EngineLink extends EventEmitter {
  constructor (repoRoot) {
    super()
    this.repoRoot = repoRoot
    this.socket = null
    this.child = null
    this.buffer = ''
    this.connected = false
    this.stopping = false
  }

  start () {
    this.connect(/* firstTry */ true)
  }

  connect (firstTry = false) {
    if (this.stopping) return
    const sock = net.createConnection({ port: ENGINE_PORT, host: ENGINE_HOST })

    sock.on('connect', () => {
      this.socket = sock
      this.connected = true
      this.buffer = ''
      this.emit('link', { up: true })
    })

    sock.on('data', chunk => {
      this.buffer += chunk.toString('utf8')
      let nl
      while ((nl = this.buffer.indexOf('\n')) >= 0) {
        const line = this.buffer.slice(0, nl).trim()
        this.buffer = this.buffer.slice(nl + 1)
        if (!line) continue
        try { this.emit('event', JSON.parse(line)) } catch { /* skip bad line */ }
      }
    })

    sock.on('error', () => {})

    sock.on('close', () => {
      const wasConnected = this.connected
      this.connected = false
      this.socket = null
      if (this.stopping) return
      if (wasConnected) this.emit('link', { up: false })
      if (firstTry && !wasConnected && !this.child) this.spawnEngine()
      setTimeout(() => this.connect(), 1000)
    })
  }

  spawnEngine () {
    const [cmd, ...args] = DEFAULT_ENGINE_CMD.split(' ')
    this.child = spawn(cmd, args, { cwd: this.repoRoot, stdio: ['ignore', 'pipe', 'pipe'] })
    this.child.stdout.on('data', d => this.emit('log', d.toString()))
    this.child.stderr.on('data', d => this.emit('log', d.toString()))
    this.child.on('exit', code => {
      // The default command is the engine-ctl launcher, which exits once the
      // engine is daemonized — only a non-zero exit is noteworthy.
      this.emit('log', code === 0 ? 'engine launcher done (engine running detached)\n'
                                  : `engine launcher failed with code ${code}\n`)
      this.child = null
    })
  }

  send (msg) {
    if (this.connected && this.socket) {
      this.socket.write(JSON.stringify(msg) + '\n')
      return true
    }
    return false
  }

  stop () {
    this.stopping = true
    this.send({ cmd: 'quit' })
    if (this.socket) this.socket.destroy()
    if (this.child) {
      const child = this.child
      setTimeout(() => { try { child.kill() } catch {} }, 500)
    }
  }
}
