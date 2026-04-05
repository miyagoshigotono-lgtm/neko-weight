// ===== STATE =====
let state = {
  pets: [],        // { id, name, avatar, photo }
  settings: {
    method: 'direct',    // 'direct' | 'hug'
    recordHuman: false,
    useVoice: false,
    notifyEnabled: false,
    notifyDay: 1
  },
  records: [],     // { date, entries: [{id, weight, skipped}] }
  currentMeasure: {},   // { id -> { weight, skipped, done } }
  editingPetId: null,
  selectedPetId: null,  // for records tab
  currentModalId: null, // which entity is being measured
  chart: null,
  recognition: null,
};

const EMOJIS = ['🐱','🐈','🐈‍⬛','😺','😸','🦁'];
const HUMAN_ID = '__human__';

// ===== INIT =====
function init() {
  loadData();
  updateHomeDate();
  renderHome();
  renderPetList();
  applySettings();
  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('sw.js').catch(() => {});
  }
}

function loadData() {
  try {
    const s = localStorage.getItem('neko_settings');
    if (s) state.settings = { ...state.settings, ...JSON.parse(s) };
    const p = localStorage.getItem('neko_pets');
    if (p) state.pets = JSON.parse(p);
    const r = localStorage.getItem('neko_records');
    if (r) state.records = JSON.parse(r);
  } catch(e) {}
}

function saveData() {
  localStorage.setItem('neko_settings', JSON.stringify(state.settings));
  localStorage.setItem('neko_pets', JSON.stringify(state.pets));
  localStorage.setItem('neko_records', JSON.stringify(state.records));
}

// ===== NAVIGATION =====
function showScreen(name) {
  document.querySelectorAll('.screen').forEach(s => s.classList.remove('active'));
  document.querySelectorAll('.nav-btn').forEach(b => b.classList.remove('active'));
  document.getElementById('screen-' + name).classList.add('active');
  document.getElementById('nav-' + name)?.classList.add('active');

  if (name === 'home') renderHome();
  if (name === 'records') renderRecords();
  if (name === 'pets') renderPetList();
}

// ===== HOME =====
function updateHomeDate() {
  const now = new Date();
  const days = ['日','月','火','水','木','金','土'];
  document.getElementById('home-date').textContent =
    `${now.getMonth()+1}月${now.getDate()}日（${days[now.getDay()]}）`;
}

function renderHome() {
  document.getElementById('home-pet-count').textContent = state.pets.length;

  // Last measure date
  const lastRec = state.records[state.records.length - 1];
  if (lastRec) {
    document.getElementById('home-last-date').textContent = formatDate(lastRec.date);
    document.getElementById('home-last-sub').textContent = daysAgo(lastRec.date);
  } else {
    document.getElementById('home-last-date').textContent = '—';
    document.getElementById('home-last-sub').textContent = 'まだ測定なし';
  }

  // Pet scroll
  const scroll = document.getElementById('home-pet-scroll');
  if (state.pets.length === 0) {
    scroll.innerHTML = `<div style="padding:12px 0;color:var(--muted);font-size:13px;white-space:nowrap;padding-left:4px;">ねこを登録してください 🐾</div>`;
    return;
  }
  scroll.innerHTML = state.pets.map(pet => {
    const last = getLastWeight(pet.id);
    const prev = getPrevWeight(pet.id);
    const diff = (last !== null && prev !== null) ? (last - prev) : null;
    const diffStr = diff === null ? '' : (diff > 0 ? `+${diff.toFixed(2)}kg` : diff < 0 ? `${diff.toFixed(2)}kg` : `±0`);
    const diffClass = diff === null ? '' : diff > 0 ? 'diff-up' : diff < 0 ? 'diff-down' : 'diff-same';
    return `
      <div class="pet-mini-card" onclick="showScreen('records');selectPetTab('${pet.id}')">
        <div class="pet-mini-avatar">${avatarHtml(pet, 56)}</div>
        <div class="pet-mini-name">${escHtml(pet.name)}</div>
        <div class="pet-mini-weight">${last !== null ? last.toFixed(2)+'kg' : '未測定'}</div>
        ${diffStr ? `<div class="pet-mini-diff ${diffClass}">${diffStr}</div>` : ''}
      </div>`;
  }).join('');
}

// ===== MEASUREMENT =====
function startMeasurement() {
  state.currentMeasure = {};
  showScreen('measure');

  const label = state.settings.method === 'hug' ? '抱っこ測定' : '直接測定';
  document.getElementById('meas-method-label').textContent = label;

  renderMeasCards();
  updateCompleteBtn();
}

function renderMeasCards() {
  const grid = document.getElementById('meas-cards');
  const entities = getMeasureEntities();

  grid.innerHTML = entities.map(e => {
    const m = state.currentMeasure[e.id];
    const done = m?.done;
    const skipped = m?.skipped;
    const last = e.id === HUMAN_ID ? getLastHumanWeight() : getLastWeight(e.id);
    return `
      <div class="meas-card ${done && !skipped ? 'done' : ''} ${skipped ? 'skipped' : ''}" 
           id="mcard-${e.id}" onclick="openInputModal('${e.id}')">
        <div class="meas-avatar">${avatarHtml(e, 64)}</div>
        <div class="meas-card-name">${escHtml(e.name)}</div>
        <div class="meas-card-last">${last !== null ? '前回 '+last.toFixed(2)+'kg' : '初回測定'}</div>
        ${done && !skipped ? `<div style="font-size:11px;color:var(--green);margin-top:4px;font-weight:700;">${state.currentMeasure[e.id].weight.toFixed(2)}kg</div>` : ''}
        ${skipped ? `<div style="font-size:11px;color:var(--muted);margin-top:4px;">未測定</div>` : ''}
      </div>`;
  }).join('');
}

function getMeasureEntities() {
  const entities = [];
  if (state.settings.recordHuman) {
    entities.push({ id: HUMAN_ID, name: 'わたし', avatar: '👤', photo: null });
  }
  state.pets.forEach(p => entities.push(p));
  return entities;
}

function openInputModal(entityId) {
  state.currentModalId = entityId;
  const e = entityId === HUMAN_ID
    ? { name: 'わたし' }
    : state.pets.find(p => p.id === entityId);
  if (!e) return;

  document.getElementById('modal-title').textContent = `${e.name}の体重`;
  document.getElementById('weight-input').value = '';
  document.getElementById('voice-status').textContent = '';

  const existing = state.currentMeasure[entityId];
  if (existing?.weight) document.getElementById('weight-input').value = existing.weight;

  const voiceBtn = document.getElementById('voice-btn');
  voiceBtn.style.display = state.settings.useVoice ? 'flex' : 'none';
  voiceBtn.classList.remove('listening');

  document.getElementById('input-modal').classList.add('open');
  setTimeout(() => document.getElementById('weight-input').focus(), 300);
}

function closeInputModal() {
  document.getElementById('input-modal').classList.remove('open');
  stopVoice();
}

function confirmMeasure() {
  const val = parseFloat(document.getElementById('weight-input').value);
  if (isNaN(val) || val <= 0) {
    showToast('体重を入力してください 🐕');
    return;
  }
  state.currentMeasure[state.currentModalId] = { weight: val, skipped: false, done: true };
  closeInputModal();
  renderMeasCards();
  updateCompleteBtn();
}

function skipMeasure() {
  state.currentMeasure[state.currentModalId] = { weight: null, skipped: true, done: true };
  closeInputModal();
  renderMeasCards();
  updateCompleteBtn();
}

function updateCompleteBtn() {
  const entities = getMeasureEntities();
  const allDone = entities.length > 0 && entities.every(e => state.currentMeasure[e.id]?.done);
  const btn = document.getElementById('complete-btn');
  btn.classList.toggle('ready', allDone);
}

function completeMeasurement() {
  const entities = getMeasureEntities();
  const allDone = entities.every(e => state.currentMeasure[e.id]?.done);
  if (!allDone) {
    showToast('全員の入力が終わってません 🐾');
    return;
  }

  const today = todayStr();
  const entries = [];

  entities.forEach(e => {
    const m = state.currentMeasure[e.id];
    if (!m.skipped) {
      let weight = m.weight;
      // 抱っこで測定: ペットの体重 = 抱っこ重量 - 人の体重
      if (state.settings.method === 'hug' && e.id !== HUMAN_ID) {
        const humanEntry = state.currentMeasure[HUMAN_ID];
        if (humanEntry && !humanEntry.skipped && humanEntry.weight) {
          weight = m.weight - humanEntry.weight;
          if (weight < 0) weight = 0;
        }
      }
      entries.push({ id: e.id, weight: Math.round(weight * 100) / 100, skipped: false });
    } else {
      entries.push({ id: e.id, weight: null, skipped: true });
    }
  });

  // Remove same-day record if exists
  state.records = state.records.filter(r => r.date !== today);
  state.records.push({ date: today, entries });
  state.records.sort((a, b) => a.date.localeCompare(b.date));
  saveData();

  showToast('測定完了！おつかれさまです 🐱');
  showScreen('home');
}

// ===== VOICE INPUT =====
function toggleVoice() {
  const btn = document.getElementById('voice-btn');
  if (btn.classList.contains('listening')) {
    stopVoice();
  } else {
    startVoice();
  }
}

function startVoice() {
  const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
  if (!SpeechRecognition) {
    showToast('このブラウザは音声入力非対応です');
    return;
  }
  state.recognition = new SpeechRecognition();
  state.recognition.lang = 'ja-JP';
  state.recognition.continuous = false;
  state.recognition.interimResults = false;

  state.recognition.onstart = () => {
    document.getElementById('voice-btn').classList.add('listening');
    document.getElementById('voice-status').textContent = '🎤 聞いています...';
    document.getElementById('voice-status').classList.add('listening');
  };

  state.recognition.onresult = (event) => {
    const transcript = event.results[0][0].transcript;
    const num = parseVoiceNumber(transcript);
    if (num !== null) {
      document.getElementById('weight-input').value = num;
      document.getElementById('voice-status').textContent = `✓ "${transcript}" → ${num}kg`;
      document.getElementById('voice-status').classList.remove('listening');
    } else {
      document.getElementById('voice-status').textContent = `聞き取れませんでした（${transcript}）`;
      document.getElementById('voice-status').classList.remove('listening');
    }
    stopVoice();
  };

  state.recognition.onerror = () => {
    document.getElementById('voice-status').textContent = 'エラーが発生しました';
    document.getElementById('voice-status').classList.remove('listening');
    stopVoice();
  };

  state.recognition.onend = () => {
    document.getElementById('voice-btn').classList.remove('listening');
  };

  state.recognition.start();
}

function stopVoice() {
  if (state.recognition) {
    try { state.recognition.stop(); } catch(e) {}
    state.recognition = null;
  }
  document.getElementById('voice-btn')?.classList.remove('listening');
}

function parseVoiceNumber(text) {
  // 「4.2キロ」「420グラム」「4キロ200グラム」「4.2」など
  text = text.replace(/\s/g, '');
  // グラム -> kg
  let m = text.match(/^(\d+\.?\d*)グラム?$/);
  if (m) return Math.round(parseFloat(m[1])) / 1000;
  // ◯キロ◯◯グラム
  m = text.match(/^(\d+)キロ?(\d+)グラム?$/);
  if (m) return parseFloat(m[1]) + parseInt(m[2]) / 1000;
  // ◯キロ◯◯
  m = text.match(/^(\d+)キロ?(\d+)$/);
  if (m) return parseFloat(`${m[1]}.${m[2]}`);
  // ◯.◯キロ
  m = text.match(/^(\d+\.?\d*)キロ?$/);
  if (m) return parseFloat(m[1]);
  // 数字だけ
  m = text.match(/^(\d+\.?\d*)$/);
  if (m) return parseFloat(m[1]);
  return null;
}

// ===== RECORDS =====
function renderRecords() {
  const tabs = document.getElementById('record-tabs');
  const entities = [];
  if (state.settings.recordHuman) entities.push({ id: HUMAN_ID, name: 'わたし' });
  state.pets.forEach(p => entities.push(p));

  if (entities.length === 0) {
    tabs.innerHTML = '';
    document.getElementById('history-list').innerHTML = `<div class="empty-state"><div class="emoji">📊</div><p>ペットを登録して測定すると<br>記録が表示されるよ</p></div>`;
    return;
  }

  if (!state.selectedPetId || !entities.find(e => e.id === state.selectedPetId)) {
    state.selectedPetId = entities[0].id;
  }

  tabs.innerHTML = entities.map(e => `
    <button class="pet-tab ${e.id === state.selectedPetId ? 'active' : ''}"
            onclick="selectPetTab('${e.id}')">${escHtml(e.name)}</button>
  `).join('');

  renderPetHistory(state.selectedPetId);
}

function selectPetTab(id) {
  state.selectedPetId = id;
  renderRecords();
}

function renderPetHistory(entityId) {
  const records = state.records
    .map(r => {
      const entry = r.entries.find(e => e.id === entityId);
      return entry ? { date: r.date, weight: entry.weight, skipped: entry.skipped } : null;
    })
    .filter(Boolean)
    .filter(r => !r.skipped && r.weight !== null);

  renderChart(records);

  const list = document.getElementById('history-list');
  if (records.length === 0) {
    list.innerHTML = `<div class="empty-state"><div class="emoji">📝</div><p>まだ記録がないよ<br>測定してみよう！</p></div>`;
    return;
  }

  const reversed = [...records].reverse();
  list.innerHTML = reversed.map((r, i) => {
    const prev = reversed[i + 1];
    const diff = prev ? r.weight - prev.weight : null;
    const diffStr = diff === null ? '初回' : (diff > 0 ? `+${diff.toFixed(2)}kg` : diff < 0 ? `${diff.toFixed(2)}kg` : `±0`);
    const diffClass = diff === null ? 'diff-same' : diff > 0 ? 'diff-up' : diff < 0 ? 'diff-down' : 'diff-same';
    return `
      <div class="history-item">
        <div class="history-date">${formatDate(r.date)}</div>
        <div class="history-weight">${r.weight.toFixed(2)} <span style="font-size:13px;color:var(--muted)">kg</span></div>
        <div class="history-diff ${diffClass}">${diffStr}</div>
      </div>`;
  }).join('');
}

function renderChart(records) {
  const canvas = document.getElementById('weightChart');
  if (state.chart) { state.chart.destroy(); state.chart = null; }

  if (records.length < 2) {
    const ctx = canvas.getContext('2d');
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    canvas.parentElement.querySelector('.graph-title').nextSibling?.remove?.();
    return;
  }

  const labels = records.map(r => formatDate(r.date));
  const data = records.map(r => r.weight);

  state.chart = new Chart(canvas, {
    type: 'line',
    data: {
      labels,
      datasets: [{
        data,
        borderColor: '#d4845a',
        backgroundColor: 'rgba(212,132,90,0.1)',
        borderWidth: 2.5,
        pointBackgroundColor: '#d4845a',
        pointRadius: 4,
        tension: 0.3,
        fill: true,
      }]
    },
    options: {
      responsive: true,
      plugins: { legend: { display: false } },
      scales: {
        x: {
          ticks: { font: { size: 10 }, color: '#8a6f5e' },
          grid: { display: false }
        },
        y: {
          ticks: {
            font: { size: 10 }, color: '#8a6f5e',
            callback: v => v.toFixed(1) + 'kg'
          },
          grid: { color: 'rgba(0,0,0,0.05)' }
        }
      }
    }
  });
}

// ===== PETS =====
function renderPetList() {
  const list = document.getElementById('pet-list');
  if (state.pets.length === 0) {
    list.innerHTML = `<div class="empty-state"><div class="emoji">🐕</div><p>まだペットが登録されていないよ<br>上のボタンから登録してね</p></div>`;
    return;
  }
  list.innerHTML = state.pets.map(pet => `
    <div class="pet-list-item" onclick="openPetModal('${pet.id}')">
      <div class="pet-list-avatar">${avatarHtml(pet, 52)}</div>
      <div class="pet-list-info">
        <div class="pet-list-name">${escHtml(pet.name)}</div>
        <div class="pet-list-sub">最終: ${getLastWeight(pet.id) !== null ? getLastWeight(pet.id).toFixed(2)+'kg' : '未測定'}</div>
      </div>
      <span class="pet-list-arrow">›</span>
    </div>
  `).join('');
}

function openPetModal(petId) {
  state.editingPetId = petId;
  const modal = document.getElementById('pet-modal');
  const isNew = !petId;
  document.getElementById('pet-modal-title').textContent = isNew ? 'ペットを登録' : 'ペットを編集';
  document.getElementById('delete-pet-btn').style.display = isNew ? 'none' : 'block';

  if (isNew) {
    document.getElementById('pet-name-input').value = '';
    document.querySelectorAll('.avatar-opt').forEach((o, i) => o.classList.toggle('selected', i === 0));
    document.getElementById('photo-preview').innerHTML = '🐱';
    document.getElementById('photo-preview').style.backgroundImage = '';
  } else {
    const pet = state.pets.find(p => p.id === petId);
    document.getElementById('pet-name-input').value = pet.name;
    document.querySelectorAll('.avatar-opt').forEach(o => o.classList.toggle('selected', o.dataset.avatar === pet.avatar));
    const preview = document.getElementById('photo-preview');
    if (pet.photo) {
      preview.style.backgroundImage = `url(${pet.photo})`;
      preview.style.backgroundSize = 'cover';
      preview.textContent = '';
    } else {
      preview.style.backgroundImage = '';
      preview.textContent = pet.avatar || '🐱';
    }
  }
  modal.classList.add('open');
}

function closePetModal() {
  document.getElementById('pet-modal').classList.remove('open');
}

function selectAvatar(el) {
  document.querySelectorAll('.avatar-opt').forEach(o => o.classList.remove('selected'));
  el.classList.add('selected');
  const preview = document.getElementById('photo-preview');
  preview.textContent = el.dataset.avatar;
  preview.style.backgroundImage = '';
}

function handlePhoto(event) {
  const file = event.target.files[0];
  if (!file) return;
  const reader = new FileReader();
  reader.onload = (e) => {
    const preview = document.getElementById('photo-preview');
    preview.style.backgroundImage = `url(${e.target.result})`;
    preview.style.backgroundSize = 'cover';
    preview.textContent = '';
    preview._photoData = e.target.result;
  };
  reader.readAsDataURL(file);
}

function savePet() {
  const name = document.getElementById('pet-name-input').value.trim();
  if (!name) { showToast('名前を入力してください 🐱'); return; }

  const selectedAvatar = document.querySelector('.avatar-opt.selected')?.dataset.avatar || '🐱';
  const preview = document.getElementById('photo-preview');
  const photo = preview._photoData || null;

  if (state.editingPetId) {
    const pet = state.pets.find(p => p.id === state.editingPetId);
    pet.name = name;
    pet.avatar = selectedAvatar;
    if (photo) pet.photo = photo;
  } else {
    state.pets.push({ id: genId(), name, avatar: selectedAvatar, photo });
  }

  saveData();
  closePetModal();
  renderPetList();
  renderHome();
  showToast('保存しました 🐾');
}

function deletePet() {
  if (!confirm('この子をリストから外す？')) return;
  state.pets = state.pets.filter(p => p.id !== state.editingPetId);
  saveData();
  closePetModal();
  renderPetList();
  renderHome();
  showToast('リストから外しました');
}

// ===== SETTINGS =====
function applySettings() {
  document.getElementById('toggle-human').checked = state.settings.recordHuman;
  document.getElementById('toggle-voice').checked = state.settings.useVoice;
  document.getElementById('toggle-notify').checked = state.settings.notifyEnabled;
  document.getElementById('notify-day-display').textContent = state.settings.notifyDay;
  setMethod(state.settings.method, true);
}

function setMethod(method, silent) {
  state.settings.method = method;
  document.getElementById('method-direct').classList.toggle('selected', method === 'direct');
  document.getElementById('method-hug').classList.toggle('selected', method === 'hug');
  if (!silent) saveSetting('method', method);
}

function saveSetting(key, value) {
  state.settings[key] = value;
  saveData();
}

function changeNotifyDay(delta) {
  let day = state.settings.notifyDay + delta;
  if (day < 1) day = 1;
  if (day > 28) day = 28;
  state.settings.notifyDay = day;
  document.getElementById('notify-day-display').textContent = day;
  saveData();
  if (state.settings.notifyEnabled) scheduleNotification();
}

async function toggleNotification(enabled) {
  if (enabled) {
    if ('Notification' in window) {
      const perm = await Notification.requestPermission();
      if (perm !== 'granted') {
        showToast('通知の許可が必要です 🔔');
        document.getElementById('toggle-notify').checked = false;
        return;
      }
    }
    state.settings.notifyEnabled = true;
    scheduleNotification();
    showToast(`毎月${state.settings.notifyDay}日に通知します 🔔`);
  } else {
    state.settings.notifyEnabled = false;
  }
  saveData();
}

function scheduleNotification() {
  // Service Worker経由で通知をスケジュール
  if ('serviceWorker' in navigator && navigator.serviceWorker.controller) {
    navigator.serviceWorker.controller.postMessage({
      type: 'SCHEDULE_NOTIFY',
      day: state.settings.notifyDay
    });
  }
}

// ===== HELPERS =====
function getLastWeight(petId) {
  for (let i = state.records.length - 1; i >= 0; i--) {
    const entry = state.records[i].entries.find(e => e.id === petId && !e.skipped);
    if (entry) return entry.weight;
  }
  return null;
}

function getPrevWeight(petId) {
  let count = 0;
  for (let i = state.records.length - 1; i >= 0; i--) {
    const entry = state.records[i].entries.find(e => e.id === petId && !e.skipped);
    if (entry) {
      count++;
      if (count === 2) return entry.weight;
    }
  }
  return null;
}

function getLastHumanWeight() {
  for (let i = state.records.length - 1; i >= 0; i--) {
    const entry = state.records[i].entries.find(e => e.id === HUMAN_ID && !e.skipped);
    if (entry) return entry.weight;
  }
  return null;
}

function avatarHtml(entity, size) {
  if (entity.photo) {
    return `<img src="${entity.photo}" style="width:${size}px;height:${size}px;border-radius:50%;object-fit:cover;">`;
  }
  return `<span style="font-size:${Math.floor(size*0.5)}px;line-height:${size}px;">${entity.avatar || '🐱'}</span>`;
}

function formatDate(str) {
  const d = new Date(str);
  return `${d.getMonth()+1}/${d.getDate()}`;
}

function daysAgo(str) {
  const diff = Math.floor((new Date() - new Date(str)) / 86400000);
  if (diff === 0) return '今日';
  if (diff === 1) return '昨日';
  return `${diff}日前`;
}

function todayStr() {
  return new Date().toISOString().slice(0, 10);
}

function genId() {
  return Math.random().toString(36).slice(2, 10);
}

function escHtml(str) {
  return str.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

function showToast(msg) {
  const t = document.getElementById('toast');
  t.textContent = msg;
  t.classList.add('show');
  setTimeout(() => t.classList.remove('show'), 2500);
}

// Close modals on overlay click
document.getElementById('input-modal').addEventListener('click', function(e) {
  if (e.target === this) closeInputModal();
});
document.getElementById('pet-modal').addEventListener('click', function(e) {
  if (e.target === this) closePetModal();
});

init();
