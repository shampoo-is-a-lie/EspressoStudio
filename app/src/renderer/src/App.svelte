<script>
  import { initEngineBridge } from './stores.js'
  import TransportBar from './components/TransportBar.svelte'
  import TrackList from './components/TrackList.svelte'
  import Arrange from './components/Arrange.svelte'

  initEngineBridge()
</script>

<main>
  <TransportBar />
  <div class="workspace">
    <TrackList />
    <Arrange />
  </div>
</main>

<style>
  :global(body) {
    margin: 0;
    background: #161618;
    color: #d8d8dc;
    font-family: 'Inter', 'Cantarell', system-ui, sans-serif;
    user-select: none;
    overflow: hidden;
  }
  main { display: flex; flex-direction: column; height: 100vh; }
  .workspace { flex: 1; display: flex; overflow: hidden; }
</style>
