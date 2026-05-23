import uPlot from 'uplot';
import 'uplot/dist/uPlot.min.css';

const WINDOW_SIZE = 300;
const COLORS = [
  '#e6194b',
  '#3cb44b',
  '#4363d8',
  '#f58231',
  '#911eb4',
  '#42d4f4',
  '#f032e6',
  '#bfef45',
];

const statusEl = document.getElementById('status')!;
const chartEl = document.getElementById('chart-container')!;

let plot: uPlot | null = null;
let data: number[][] = [];
let startTime = 0;

function initChart(labels: string[]) {
  if (plot) {
    plot.destroy();
    chartEl.querySelectorAll('.uplot').forEach((el) => el.remove());
  }

  startTime = Date.now();
  data = [[]]; // data[0] = timestamps
  for (let i = 0; i < labels.length; i++) {
    data.push([]);
  }

  const series: uPlot.Series[] = [
    { label: 'Time (s)' },
    ...labels.map((label, i) => ({
      label,
      stroke: COLORS[i % COLORS.length],
      width: 1.5,
    })),
  ];

  const opts: uPlot.Options = {
    width: chartEl.clientWidth - 32,
    height: chartEl.clientHeight - 32,
    series,
    scales: {
      x: { time: false },
    },
    axes: [
      { label: 'Time (s)', stroke: '#a0a0c0', grid: { stroke: 'rgba(255,255,255,0.06)' } },
      { label: 'Value', stroke: '#a0a0c0', grid: { stroke: 'rgba(255,255,255,0.06)' } },
    ],
    cursor: { drag: { x: false, y: false } },
    legend: { show: true },
  };

  plot = new uPlot(opts, data as uPlot.AlignedData, chartEl);

  const ro = new ResizeObserver(() => {
    if (plot) {
      plot.setSize({
        width: chartEl.clientWidth - 32,
        height: chartEl.clientHeight - 32,
      });
    }
  });
  ro.observe(chartEl);

  statusEl.textContent = `Connected — ${labels.length} channels: ${labels.join(', ')}`;
  statusEl.className = 'connected';
}

function connect() {
  const ws = new WebSocket(`ws://${location.hostname}:8081`);

  statusEl.textContent = 'Connecting...';
  statusEl.className = 'connecting';

  ws.onopen = () => {
    statusEl.textContent = 'Connected, waiting for data...';
    statusEl.className = 'connected';
  };

  ws.onmessage = (event) => {
    const msg = JSON.parse(event.data);

    if (msg.type === 'labels') {
      initChart(msg.data);
      return;
    }

    if (msg.type === 'data' && plot) {
      const elapsed = (msg.t - startTime) / 1000;
      data[0].push(parseFloat(elapsed.toFixed(3)));

      for (let i = 0; i < msg.data.length; i++) {
        if (data[i + 1]) {
          data[i + 1].push(msg.data[i]);
        }
      }

      if (data[0].length > WINDOW_SIZE) {
        for (let i = 0; i < data.length; i++) {
          data[i].shift();
        }
      }

      plot.setData(data as uPlot.AlignedData);
    }
  };

  ws.onclose = () => {
    statusEl.textContent = 'Disconnected — reconnecting in 2s...';
    statusEl.className = 'disconnected';
    setTimeout(connect, 2000);
  };

  ws.onerror = () => {
    ws.close();
  };
}

connect();
