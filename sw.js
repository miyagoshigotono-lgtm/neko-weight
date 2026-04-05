const CACHE = 'neko-v1';
const FILES = ['/', '/index.html', '/app.js', '/manifest.json'];

self.addEventListener('install', e => {
  e.waitUntil(caches.open(CACHE).then(c => c.addAll(FILES).catch(() => {})));
  self.skipWaiting();
});

self.addEventListener('activate', e => {
  e.waitUntil(caches.keys().then(keys =>
    Promise.all(keys.filter(k => k !== CACHE).map(k => caches.delete(k)))
  ));
  self.clients.claim();
});

self.addEventListener('fetch', e => {
  e.respondWith(
    caches.match(e.request).then(r => r || fetch(e.request).catch(() => caches.match('/index.html')))
  );
});

// 通知スケジュール管理
let notifyTimer = null;

self.addEventListener('message', e => {
  if (e.data?.type === 'SCHEDULE_NOTIFY') {
    scheduleMonthlyNotify(e.data.day);
  }
});

function scheduleMonthlyNotify(targetDay) {
  if (notifyTimer) clearTimeout(notifyTimer);

  const now = new Date();
  let next = new Date(now.getFullYear(), now.getMonth(), targetDay, 9, 0, 0);
  if (next <= now) {
    next = new Date(now.getFullYear(), now.getMonth() + 1, targetDay, 9, 0, 0);
  }

  const ms = next.getTime() - now.getTime();
  notifyTimer = setTimeout(() => {
    self.registration.showNotification('ねこ体重のお時間にゃ 🐱', {
      body: `今月の体重測定をしてにゃ 🐾`,
      icon: '/icon-192.png',
      badge: '/icon-192.png',
      tag: 'neko-monthly',
    });
    // 次月も再スケジュール
    scheduleMonthlyNotify(targetDay);
  }, ms);
}

self.addEventListener('notificationclick', e => {
  e.notification.close();
  e.waitUntil(clients.openWindow('/'));
});
