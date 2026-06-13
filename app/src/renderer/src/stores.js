import { writable, get } from 'svelte/store'

export const linkUp = writable(false)
export const hello = writable(null)
export const transport = writable({ playing: false, recording: false, position: 0, levelL: 0, levelR: 0, looping: false, loopStart: 0, loopEnd: 0 })
export const tracks = writable([])
export const editLength = writable(0)
export const peaks = writable({}) // clipId -> { pps, data }
export const exportResult = writable(null)
export const logLines = writable([])
export const selection = writable(null) // { track, clip, id } or null
export const knownPlugins = writable({ scanning: false, plugins: [] })
export const fxTrack = writable(null) // track index whose FX panel is open
export const rigTrack = writable(null) // track index whose guitar rig is open

export const send = (msg) => window.espresso.send(msg)

let started = false

export function initEngineBridge () {
  if (started) return
  started = true

  window.espresso.onLink(({ up }) => linkUp.set(up))
  window.espresso.onLog((line) => logLines.update(l => [...l.slice(-100), line]))

  window.espresso.onEvent((ev) => {
    if (ev.event === 'hello') {
      hello.set(ev)
    } else if (ev.event === 'state') {
      transport.set({
        playing: ev.playing,
        recording: ev.recording,
        position: ev.position,
        levelL: ev.levelL,
        levelR: ev.levelR,
        looping: ev.looping,
        loopStart: ev.loopStart,
        loopEnd: ev.loopEnd
      })
    } else if (ev.event === 'tracks') {
      tracks.set(ev.tracks)
      editLength.set(ev.editLength)
      const have = get(peaks)
      ev.tracks.forEach((t, ti) => t.clips.forEach((c, ci) => {
        if (!have[c.id]) send({ cmd: 'peaks', track: ti, clip: ci })
      }))
      // re-anchor selection by clip id — indices shift after split/delete
      const sel = get(selection)
      if (sel) {
        let found = null
        ev.tracks.forEach((t, ti) => t.clips.forEach((c, ci) => {
          if (c.id === sel.id) found = { track: ti, clip: ci, id: c.id }
        }))
        selection.set(found)
      }
    } else if (ev.event === 'peaks') {
      peaks.update(p => ({ ...p, [ev.id]: { pps: ev.peaksPerSecond, data: ev.data } }))
    } else if (ev.event === 'exported') {
      exportResult.set(ev)
    } else if (ev.event === 'knownPlugins') {
      knownPlugins.set({ scanning: ev.scanning, plugins: ev.plugins })
    }
  })
}
