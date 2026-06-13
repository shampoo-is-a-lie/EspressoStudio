import { contextBridge, ipcRenderer } from 'electron'

contextBridge.exposeInMainWorld('espresso', {
  send: (msg) => ipcRenderer.send('engine:cmd', msg),
  onEvent: (cb) => ipcRenderer.on('engine:event', (_e, data) => cb(data)),
  onLink: (cb) => ipcRenderer.on('engine:link', (_e, data) => cb(data)),
  onLog: (cb) => ipcRenderer.on('engine:log', (_e, data) => cb(data)),
  openFile: (opts) => ipcRenderer.invoke('dialog:openFile', opts)
})
