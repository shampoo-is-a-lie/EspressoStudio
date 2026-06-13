import { app, BrowserWindow, ipcMain, dialog } from 'electron'
import path from 'path'
import { EngineLink } from './engineLink.js'

const repoRoot = path.resolve(__dirname, '..', '..', '..')
let win = null
let engine = null

function createWindow () {
  win = new BrowserWindow({
    width: 980,
    height: 560,
    minWidth: 720,
    minHeight: 420,
    backgroundColor: '#161618',
    autoHideMenuBar: true,
    title: 'EspressoStudio',
    webPreferences: {
      preload: path.join(__dirname, '../preload/index.js'),
      contextIsolation: true,
      nodeIntegration: false
    }
  })

  if (process.env.ELECTRON_RENDERER_URL) {
    win.loadURL(process.env.ELECTRON_RENDERER_URL)
  } else {
    win.loadFile(path.join(__dirname, '../renderer/index.html'))
  }
}

app.whenReady().then(() => {
  createWindow()

  engine = new EngineLink(repoRoot)
  const forward = (channel) => (data) => {
    if (win && !win.isDestroyed()) win.webContents.send(channel, data)
  }
  engine.on('event', forward('engine:event'))
  engine.on('link', forward('engine:link'))
  engine.on('log', forward('engine:log'))
  engine.start()

  ipcMain.on('engine:cmd', (_e, msg) => engine.send(msg))

  // Native file picker for guitar-rig assets (.nam captures, IR files). Returns
  // the chosen absolute path, or null if cancelled.
  ipcMain.handle('dialog:openFile', async (_e, opts = {}) => {
    const res = await dialog.showOpenDialog(win, {
      title: opts.title ?? 'Choose file',
      properties: ['openFile'],
      filters: opts.filters ?? []
    })
    return res.canceled || res.filePaths.length === 0 ? null : res.filePaths[0]
  })
})

app.on('before-quit', () => { if (engine) engine.stop() })
app.on('window-all-closed', () => app.quit())
