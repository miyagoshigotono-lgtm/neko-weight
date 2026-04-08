// ===== CONSTANTS =====
const HUMAN_ID = '__human__';
const SAMPLE_PET_ID = '__dakkokun__';
 
// ===== STATE =====
let state = {
  pets: [],
  settings: {
    method: 'direct',
    recordHuman: false,
    useVoice: false,
    notifyEnabled: false,
    notifyInterval: 'monthly',
    notifyDay: 1,
  },
  records: [],
  currentMeasure: {},
  editingPetId: null,
  selectedPetId: null,
  currentModalId: null,
  chart: null,
  recognition: null,
};
 
// ===== INIT =====
function init() {
  loadData();
  if (!localStorage.getItem('neko_initialized')) {
    insertSampleData();
    localStorage.setItem('neko_initialized', '1');
  }
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
 
// ===== SAMPLE DATA =====
function insertSampleData() {
  state.pets.push({ id: SAMPLE_PET_ID, name: 'だっこくん', avatar: '🐱', photo: null });
  state.settings.recordHuman = true;
  const today = new Date();
  const humanBase = 60.0;
  const dakkoCurve = [0.35, 0.55, 0.80, 1.10, 1.45, 1.85, 2.25, 2.60, 2.90, 3.15, 3.35, 3.50];
  for (let i = 11; i >= 0; i--) {
    const d = new Date(today.getFullYear(), today.getMonth() - i, 15);
    const dateStr = d.toISOString().slice(0, 10);
    const humanWeight = Math.round((humanBase + (Math.random() * 4 - 2)) * 10) / 10;
    const dakkoWeight = dakkoCurve[11 - i] + Math.round((Math.random() * 0.06 - 0.03) * 100) / 100;
    state.records.push({
      date: dateStr,
      entries: [
        { id: HUMAN_ID, weight: humanWeight, skipped: false },
        { id: SAMPLE_PET_ID, weight: Math.round(dakkoWeight * 100) / 100, skipped: false },
      ]
    });
  }
  state.records.sort((a, b) => a.date.localeCompare(b.date));
  saveData();
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
  const lastRec = state.records[state.records.length - 1];
  if (lastRec) {
    document.getElementById('home-last-date').textContent = formatDate(lastRec.date);
    document.getElementById('home-last-sub').textContent = daysAgo(lastRec.date);
  } else {
    document.getElementById('home-last-date').textContent = '—';
    document.getElementById('home-last-sub').textContent = 'まだ測定なし';
  }
  const scroll = document.getElementById('home-pet-scroll');
  if (state.pets.length === 0) {
    scroll.innerHTML = `<div style="padding:12px 0;color:var(--muted);font-size:13px;padding-left:4px;">ペットを登録してね 🐾</div>`;
    return;
  }
  scroll.innerHTML = state.pets.map(pet => {
    const last = getLastWeight(pet.id);
    const prev = getPrevWeight(pet.id);
    const diff = (last !== null && prev !== null) ? (last - prev) : null;
    const diffStr = diff === null ? '' : (diff > 0 ? `+${diff.toFixed(2)}kg` : diff < 0 ? `${diff.toFixed(2)}kg` : `±0`);
    const diffClass = diff === null ? '' : diff > 0 ? 'diff-up' : diff < 0 ? 'diff-down' : 'diff-same';
    const isSample = pet.id === SAMPLE_PET_ID;
    return `
      <div class="pet-mini-card" onclick="showScreen('records');selectPetTab('${pet.id}')">
        <div class="pet-mini-avatar">${avatarHtml(pet, 56)}</div>
        <div class="pet-mini-name">${escHtml(pet.name)}${isSample ? '<span class="sample-badge">サンプル</span>' : ''}</div>
        <div class="pet-mini-weight">${last !== null ? last.toFixed(2)+'kg' : '未測定'}</div>
        ${diffStr ? `<div class="pet-mini-diff ${diffClass}">${diffStr}</div>` : ''}
      </div>`;
  }).join('');
}
 
// ===== MEASUREMENT =====
function startMeasurement() {
  state.currentMeasure = {};
  showScreen('measure');
  document.getElementById('meas-method-label').textContent =
    state.settings.method === 'hug' ? '抱っこ測定' : '直接測定';
  renderMeasCards();
  updateCompleteBtn();
}
 
function getMeasureEntities() {
  const entities = [];
  if (state.settings.recordHuman) {
    entities.push({ id: HUMAN_ID, name: 'わたし', avatar: '👤', photo: null });
  }
  state.pets.forEach(p => entities.push(p));
  return entities;
}
 
function renderMeasCards() {
  const grid = document.getElementById('meas-cards');
  const entities = getMeasureEntities();
  if (entities.length === 0) {
    grid.innerHTML = `<div class="empty-state" style="grid-column:span 2"><div class="emoji">🐾</div><p>ペットを登録してから<br>測定してね</p></div>`;
    return;
  }
  grid.innerHTML = entities.map(e => {
    const m = state.currentMeasure[e.id];
    const done = m?.done;
    const skipped = m?.skipped;
    const last = e.id === HUMAN_ID ? getLastHumanWeight() : getLastWeight(e.id);
    return `
      <div class="meas-card ${done && !skipped ? 'done' : ''} ${skipped ? 'skipped' : ''}"
           onclick="openInputModal('${e.id}')">
        <div class="meas-avatar">${avatarHtml(e, 64)}</div>
        <div class="meas-card-name">${escHtml(e.name)}</div>
        <div class="meas-card-last">${last !== null ? '前回 '+last.toFixed(2)+'kg' : '初回測定'}</div>
        ${done && !skipped ? `<div style="font-size:11px;color:var(--green);margin-top:4px;font-weight:700;">${state.currentMeasure[e.id].weight.toFixed(2)}kg</div>` : ''}
        ${skipped ? `<div style="font-size:11px;color:var(--muted);margin-top:4px;">今回はスキップ</div>` : ''}
      </div>`;
  }).join('');
}
 
function openInputModal(entityId) {
  state.currentModalId = entityId;
  const e = entityId === HUMAN_ID
    ? { name: 'わたし' }
    : state.pets.find(p => p.id === entityId);
  if (!e) return;
 
  document.getElementById('modal-title').textContent = `${e.name}の体重`;
  const existing = state.currentMeasure[entityId];
  document.getElementById('weight-input').value = existing?.weight || '';
  document.getElementById('voice-status').textContent = '';
  document.getElementById('voice-status').classList.remove('listening');
 
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
  if (isNaN(val) || val <= 0) { showToast('体重を入力してね 🐾'); return; }
  state.currentMeasure[state.currentModalId] = { weight: val, skipped: false, done: true };
  closeInputModal();
  renderMeasCards();
  updateCompleteBtn();
}
 
function skipMeasure() {
  // スキップ = 今回の記録なし（前回値を参照するのではなく単純に記録しない）
  state.currentMeasure[state.currentModalId] = { weight: null, skipped: true, done: true };
  closeInputModal();
  renderMeasCards();
  updateCompleteBtn();
}
 
function updateCompleteBtn() {
  const entities = getMeasureEntities();
  // 1人でも入力済み（スキップ含む）なら完了ボタンを有効化
  const anyDone = entities.some(e => state.currentMeasure[e.id]?.done);
  document.getElementById('complete-btn').classList.toggle('ready', anyDone);
}
 
function completeMeasurement() {
  const entities = getMeasureEntities();
  const anyDone = entities.some(e => state.currentMeasure[e.id]?.done);
  if (!anyDone) {
    showToast('少なくとも1匹は測定してね 🐾'); return;
  }
 
  const today = todayStr();
 
  // 今日すでに記録がある場合はマージする
  const existingIdx = state.records.findIndex(r => r.date === today);
  let existingEntries = existingIdx >= 0 ? [...state.records[existingIdx].entries] : [];
 
  entities.forEach(e => {
    const m = state.currentMeasure[e.id];
    if (!m?.done) return; // 今回タッチしていない子はそのまま
 
    if (m.skipped) {
      // スキップした子は今日の記録から除外（前の記録があればそちらを参照）
      existingEntries = existingEntries.filter(en => en.id !== e.id);
      return;
    }
 
    let weight = m.weight;
    if (state.settings.method === 'hug' && e.id !== HUMAN_ID) {
      const hw = state.currentMeasure[HUMAN_ID];
      if (hw && !hw.skipped && hw.weight) {
        weight = Math.max(0, m.weight - hw.weight);
      }
    }
    weight = Math.round(weight * 100) / 100;
 
    // 既存エントリを上書き or 追加
    const ei = existingEntries.findIndex(en => en.id === e.id);
    if (ei >= 0) {
      existingEntries[ei] = { id: e.id, weight, skipped: false };
    } else {
      existingEntries.push({ id: e.id, weight, skipped: false });
    }
  });
 
  if (existingEntries.length === 0) {
    // 記録するものが何もない場合
    if (existingIdx >= 0) state.records.splice(existingIdx, 1);
  } else {
    const newRecord = { date: today, entries: existingEntries };
    if (existingIdx >= 0) {
      state.records[existingIdx] = newRecord;
    } else {
      state.records.push(newRecord);
    }
  }
 
  state.records.sort((a, b) => a.date.localeCompare(b.date));
  saveData();
  showToast('測定完了！お疲れさまでした 🐾');
  showScreen('home');
}
 
// ===== VOICE =====
function toggleVoice() {
  const btn = document.getElementById('voice-btn');
  btn.classList.contains('listening') ? stopVoice() : startVoice();
}
 
function startVoice() {
  const SR = window.SpeechRecognition || window.webkitSpeechRecognition;
  if (!SR) { showToast('このブラウザは音声入力非対応です'); return; }
  state.recognition = new SR();
  state.recognition.lang = 'ja-JP';
  state.recognition.continuous = false;
  state.recognition.interimResults = false;
 
  state.recognition.onstart = () => {
    const btn = document.getElementById('voice-btn');
    btn.classList.add('listening');
    btn.textContent = '⏹';
    const vs = document.getElementById('voice-status');
    vs.textContent = '🎤 話してください...';
    vs.classList.add('listening');
  };
 
  state.recognition.onresult = (event) => {
    const transcript = event.results[0][0].transcript;
    const num = parseVoiceNumber(transcript);
    const vs = document.getElementById('voice-status');
    vs.classList.remove('listening');
    if (num !== null) {
      document.getElementById('weight-input').value = num;
      vs.textContent = `✓ 「${transcript}」→ ${num}kg`;
    } else {
      vs.textContent = `聞き取れませんでした（${transcript}）`;
    }
    stopVoice();
  };
 
  state.recognition.onerror = (e) => {
    const vs = document.getElementById('voice-status');
    vs.classList.remove('listening');
    if (e.error === 'no-speech') {
      vs.textContent = '音声が検出されませんでした';
    } else if (e.error === 'not-allowed') {
      vs.textContent = 'マイクの許可が必要です';
    } else {
      vs.textContent = 'エラーが発生しました';
    }
    stopVoice();
  };
 
  state.recognition.onend = () => {
    const btn = document.getElementById('voice-btn');
    if (btn) { btn.classList.remove('listening'); btn.textContent = '🎤'; }
  };
 
  state.recognition.start();
}
 
function stopVoice() {
  if (state.recognition) {
    try { state.recognition.stop(); } catch(e) {}
    state.recognition = null;
  }
  const btn = document.getElementById('voice-btn');
  if (btn) { btn.classList.remove('listening'); btn.textContent = '🎤'; }
}
 
function parseVoiceNumber(text) {
  text = text.replace(/\s/g, '').replace(/キロメートル|ｋｍ|km/gi, 'キロ').replace(/てん/g, '.');
  let m;
  m = text.match(/^(\d+\.?\d*)グラム?$/); if (m) return Math.round(parseFloat(m[1])) / 1000;
  m = text.match(/^(\d+)キロ?(\d+)グラム?$/); if (m) return parseFloat(m[1]) + parseInt(m[2]) / 1000;
  m = text.match(/^(\d+)キロ?(\d+)$/); if (m) return parseFloat(`${m[1]}.${m[2]}`);
  m = text.match(/^(\d+\.?\d*)キロ?$/); if (m) return parseFloat(m[1]);
  m = text.match(/^(\d+\.?\d*)$/); if (m) return parseFloat(m[1]);
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
  // スキップされていない実測値のみ抽出
  const records = state.records
    .map(r => {
      const entry = r.entries?.find(e => e.id === entityId);
      return entry && !entry.skipped && entry.weight !== null ? { date: r.date, weight: entry.weight } : null;
    })
    .filter(Boolean);
 
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
        <div class="history-date">${formatDateLong(r.date)}</div>
        <div class="history-weight">${r.weight.toFixed(2)} <span style="font-size:13px;color:var(--muted)">kg</span></div>
        <div class="history-diff ${diffClass}">${diffStr}</div>
      </div>`;
  }).join('');
}
 
function renderChart(records) {
  const canvas = document.getElementById('weightChart');
  if (state.chart) { state.chart.destroy(); state.chart = null; }
  if (records.length < 2) return;
  state.chart = new Chart(canvas, {
    type: 'line',
    data: {
      labels: records.map(r => formatDate(r.date)),
      datasets: [{
        data: records.map(r => r.weight),
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
        x: { ticks: { font: { size: 10 }, color: '#8a6f5e' }, grid: { display: false } },
        y: {
          ticks: { font: { size: 10 }, color: '#8a6f5e', callback: v => v.toFixed(1)+'kg' },
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
    list.innerHTML = `<div class="empty-state"><div class="emoji">🐾</div><p>まだペットが登録されていないよ<br>上のボタンから登録してね</p></div>`;
    return;
  }
  list.innerHTML = state.pets.map(pet => {
    const isSample = pet.id === SAMPLE_PET_ID;
    return `
      <div class="pet-list-item" onclick="openPetModal('${pet.id}')">
        <div class="pet-list-avatar">${avatarHtml(pet, 52)}</div>
        <div class="pet-list-info">
          <div class="pet-list-name">${escHtml(pet.name)}${isSample ? '<span class="sample-badge">サンプル</span>' : ''}</div>
          <div class="pet-list-sub">最終: ${getLastWeight(pet.id) !== null ? getLastWeight(pet.id).toFixed(2)+'kg' : '未測定'}</div>
        </div>
        <span class="pet-list-arrow">›</span>
      </div>`;
  }).join('');
}
 
function openPetModal(petId) {
  state.editingPetId = petId;
  const isNew = !petId;
  document.getElementById('pet-modal-title').textContent = isNew ? 'ペットを登録' : 'ペットを編集';
  document.getElementById('delete-pet-btn').style.display = isNew ? 'none' : 'block';
  const preview = document.getElementById('photo-preview');
  preview._photoData = null;
  if (isNew) {
    document.getElementById('pet-name-input').value = '';
    document.querySelectorAll('.avatar-opt').forEach((o, i) => o.classList.toggle('selected', i === 0));
    preview.style.backgroundImage = '';
    preview.style.backgroundSize = '';
    preview.textContent = '🐶';
  } else {
    const pet = state.pets.find(p => p.id === petId);
    document.getElementById('pet-name-input').value = pet.name;
    document.querySelectorAll('.avatar-opt').forEach(o => o.classList.toggle('selected', o.dataset.avatar === pet.avatar));
    if (pet.photo) {
      preview.style.backgroundImage = `url(${pet.photo})`;
      preview.style.backgroundSize = 'cover';
      preview.textContent = '';
    } else {
      preview.style.backgroundImage = '';
      preview.textContent = pet.avatar || '🐶';
    }
  }
  document.getElementById('pet-modal').classList.add('open');
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
  preview._photoData = null;
}
 
function handlePhoto(event) {
  const file = event.target.files[0];
  if (!file) return;
  // 画像を圧縮してからbase64保存
  const reader = new FileReader();
  reader.onload = (e) => {
    const img = new Image();
    img.onload = () => {
      const canvas = document.createElement('canvas');
      const MAX = 200; // 200x200pxに圧縮
      let w = img.width, h = img.height;
      if (w > h) { if (w > MAX) { h = h * MAX / w; w = MAX; } }
      else { if (h > MAX) { w = w * MAX / h; h = MAX; } }
      canvas.width = w; canvas.height = h;
      canvas.getContext('2d').drawImage(img, 0, 0, w, h);
      const compressed = canvas.toDataURL('image/jpeg', 0.7);
      const preview = document.getElementById('photo-preview');
      preview.style.backgroundImage = `url(${compressed})`;
      preview.style.backgroundSize = 'cover';
      preview.textContent = '';
      preview._photoData = compressed;
    };
    img.src = e.target.result;
  };
  reader.readAsDataURL(file);
}
 
function savePet() {
  const name = document.getElementById('pet-name-input').value.trim();
  if (!name) { showToast('名前を入力してね 🐾'); return; }
  const selectedAvatar = document.querySelector('.avatar-opt.selected')?.dataset.avatar || '🐶';
  const preview = document.getElementById('photo-preview');
  const photo = preview._photoData || null;
 
  try {
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
  } catch(e) {
    // localStorage容量オーバーの場合、写真なしで保存を試みる
    if (state.editingPetId) {
      const pet = state.pets.find(p => p.id === state.editingPetId);
      pet.name = name;
      pet.avatar = selectedAvatar;
    } else {
      state.pets.push({ id: genId(), name, avatar: selectedAvatar, photo: null });
    }
    try {
      saveData();
      closePetModal();
      renderPetList();
      renderHome();
      showToast('写真は保存できませんでした（容量不足）');
    } catch(e2) {
      showToast('保存に失敗しました。古いデータを削除してね');
    }
  }
}
 
function deletePet() {
  const pet = state.pets.find(p => p.id === state.editingPetId);
  const name = pet ? pet.name : 'このペット';
  if (!confirm(`「${name}」をリストから外しますか？\n測定記録もすべて削除されます。`)) return;
  state.records = state.records.map(r => ({
    ...r,
    entries: r.entries.filter(e => e.id !== state.editingPetId)
  })).filter(r => r.entries.length > 0);
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
  setMethod(state.settings.method, true);
  setNotifyInterval(state.settings.notifyInterval || 'monthly', true);
  updateNotifyDayUI();
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
 
function setNotifyInterval(interval, silent) {
  state.settings.notifyInterval = interval;
  ['daily','weekly','monthly'].forEach(i => {
    document.getElementById('interval-' + i)?.classList.toggle('selected', i === interval);
  });
  updateNotifyDayUI();
  if (!silent) saveData();
}
 
function updateNotifyDayUI() {
  const interval = state.settings.notifyInterval;
  const row = document.getElementById('notify-day-row');
  const label = document.getElementById('notify-day-label');
  const desc = document.getElementById('notify-day-desc');
  const unit = document.getElementById('notify-day-unit');
  const display = document.getElementById('notify-day-display');
  if (interval === 'daily') {
    row.style.display = 'none';
  } else if (interval === 'weekly') {
    row.style.display = 'flex';
    label.textContent = '通知する曜日';
    desc.textContent = '';
    unit.textContent = '';
    const day = Math.min(Math.max(state.settings.notifyDay, 1), 7);
    const dayNames = ['','日','月','火','水','木','金','土'];
    display.textContent = dayNames[day] + '曜日';
  } else {
    row.style.display = 'flex';
    label.textContent = '通知する日';
    desc.textContent = '毎月この日に通知';
    unit.textContent = '日';
    display.textContent = state.settings.notifyDay;
  }
}
 
function changeNotifyDay(delta) {
  const interval = state.settings.notifyInterval;
  let day = state.settings.notifyDay + delta;
  if (interval === 'weekly') {
    if (day < 1) day = 7;
    if (day > 7) day = 1;
  } else {
    if (day < 1) day = 1;
    if (day > 28) day = 28;
  }
  state.settings.notifyDay = day;
  updateNotifyDayUI();
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
    showToast('通知を設定しました 🔔');
  } else {
    state.settings.notifyEnabled = false;
  }
  saveData();
}
 
function scheduleNotification() {
  if ('serviceWorker' in navigator && navigator.serviceWorker.controller) {
    navigator.serviceWorker.controller.postMessage({
      type: 'SCHEDULE_NOTIFY',
      interval: state.settings.notifyInterval,
      day: state.settings.notifyDay,
    });
  }
}
 
function resetHumanData() {
  const recordCount = state.records.filter(r => r.entries?.some(e => e.id === HUMAN_ID && !e.skipped)).length;
  if (recordCount === 0) { showToast('削除するデータがありません'); return; }
  if (!confirm(`【わたしのデータをリセット】\n\n削除される内容：\n・人の体重記録 ${recordCount}件\n\nこの操作は元に戻せません。\n本当に削除しますか？`)) return;
  state.records = state.records.map(r => ({
    ...r,
    entries: r.entries.filter(e => e.id !== HUMAN_ID)
  })).filter(r => r.entries.length > 0);
  saveData();
  showToast('わたしのデータを削除しました');
  renderHome();
}
 
// ===== HELPERS =====
function getLastWeight(petId) {
  for (let i = state.records.length - 1; i >= 0; i--) {
    const entry = state.records[i].entries?.find(e => e.id === petId && !e.skipped);
    if (entry) return entry.weight;
  }
  return null;
}
 
function getPrevWeight(petId) {
  let count = 0;
  for (let i = state.records.length - 1; i >= 0; i--) {
    const entry = state.records[i].entries?.find(e => e.id === petId && !e.skipped);
    if (entry) { count++; if (count === 2) return entry.weight; }
  }
  return null;
}
 
function getLastHumanWeight() {
  for (let i = state.records.length - 1; i >= 0; i--) {
    const entry = state.records[i].entries?.find(e => e.id === HUMAN_ID && !e.skipped);
    if (entry) return entry.weight;
  }
  return null;
}
 
function avatarHtml(entity, size) {
  if (entity.photo) {
    return `<img src="${entity.photo}" style="width:${size}px;height:${size}px;border-radius:50%;object-fit:cover;">`;
  }
  return `<span style="font-size:${Math.floor(size*0.5)}px;line-height:${size}px;">${entity.avatar || '🐾'}</span>`;
}
 
function formatDate(str) {
  const d = new Date(str);
  return `${d.getMonth()+1}/${d.getDate()}`;
}
 
function formatDateLong(str) {
  const d = new Date(str);
  return `${d.getFullYear()}.${String(d.getMonth()+1).padStart(2,'0')}.${String(d.getDate()).padStart(2,'0')}`;
}
 
function daysAgo(str) {
  const diff = Math.floor((new Date() - new Date(str)) / 86400000);
  if (diff === 0) return '今日';
  if (diff === 1) return '昨日';
  return `${diff}日前`;
}
 
function todayStr() { return new Date().toISOString().slice(0, 10); }
function genId() { return Math.random().toString(36).slice(2, 10); }
function escHtml(str) {
  return String(str).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}
 
function showToast(msg) {
  const t = document.getElementById('toast');
  t.textContent = msg;
  t.classList.add('show');
  setTimeout(() => t.classList.remove('show'), 2500);
}
 
document.getElementById('input-modal').addEventListener('click', function(e) {
  if (e.target === this) closeInputModal();
});
document.getElementById('pet-modal').addEventListener('click', function(e) {
  if (e.target === this) closePetModal();
});
 
init();
 
