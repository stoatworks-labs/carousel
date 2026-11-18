// The page's half of the crosscheck: fetch the job and the two pictures from
// crosscheck.py's server, run each cue sheet through
// window.carouselDemo.sequence (a fresh instance of the plugin compiled to
// WebAssembly, on the page's own WebGL2 context), and post the frames back,
// with the page's table of controls. crosscheck.py compares; nothing is
// judged here.
const post = (path, body) => fetch(path, { method: 'POST', body });

async function main() {
  for (let i = 0; i < 400 && !window.carouselDemo?.mounted; i += 1) await new Promise((r) => setTimeout(r, 50));
  if (!window.carouselDemo?.mounted) throw new Error('window.carouselDemo never appeared: the page did not start');
  const job = await (await fetch('/xc/job.json')).json();
  const dest = new Uint8Array(await (await fetch('/xc/dest')).arrayBuffer());
  const src = new Uint8Array(await (await fetch('/xc/src')).arrayBuffer());
  await post('/xc/table', JSON.stringify(window.carouselDemo.table));
  const glErrors = [];
  const frameBytes = job.width * job.height * 4;
  for (const run of job.runs) {
    const seq = window.carouselDemo.sequence({
      width: job.width, height: job.height, dest, src, srcWidth: job.srcWidth, srcHeight: job.srcHeight, fps: job.fps, cues: run.cues,
    });
    const [lo, hi] = run.keep;
    try {
      for (let k = 0; k < hi; k += 1) {
        const pixels = seq.step(1);
        const error = seq.error();
        if (error) glErrors.push(`${run.id} frame ${k}: 0x${error.toString(16)}`);
        if (k >= lo) await post(`/xc/out/${run.id}/${k}`, pixels.subarray(0, frameBytes));
      }
    } finally {
      seq.end();
    }
  }
  await post('/xc/notes', JSON.stringify({
    glErrors,
    shaders: window.carouselDemo.shaders.accepted,
    clock: window.carouselDemo.instant()?.clock ?? null,
  }));
  await post('/xc/done', 'ok');
}

main().catch((error) => post('/xc/error', String(error?.stack ?? error)));
