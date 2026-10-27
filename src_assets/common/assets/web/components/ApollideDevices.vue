<script setup lang="ts">
// Apollide: Artemide clients this host has seen, their versions, and one-click updates
// in both directions (host ↔ client). See src/apollide/update_link.h.
import { computed, onBeforeUnmount, onMounted, ref } from 'vue';
import { apiGet, apiPost } from '@/api/client';
import { AppButton, StatusBadge, UiIcon } from '@/components/ui';
import type { StatusTone } from '@/components/ui/types';

interface LinkClient {
  uuid: string;
  name: string;
  current: string;
  latest: string;
  state: number; // 0 latest, 1 available, 2 updating
  requested: boolean;
  connected: boolean;
  seen: number;
}
interface LinkSnapshot {
  host: { current: string; latest: string; state: number }; // 0 latest, 1 available, 2 downloading, 3 installing
  clients: LinkClient[];
}

const snapshot = ref<LinkSnapshot | null>(null);
const busy = ref<Record<string, boolean>>({});
let timer: ReturnType<typeof setInterval> | undefined;

async function load(): Promise<void> {
  try {
    snapshot.value = await apiGet<LinkSnapshot>('/api/apollide/link');
  } catch {
    // The service restarts while it updates; the next poll picks it up again
  }
}

const clients = computed(() =>
  [...(snapshot.value?.clients ?? [])].sort((a, b) => Number(b.connected) - Number(a.connected) || b.seen - a.seen),
);

function newer(a: string, b: string): boolean {
  const p = (v: string) => v.split('.').map((x) => Number.parseInt(x, 10) || 0);
  const [x, y] = [p(a), p(b)];
  for (let i = 0; i < 3; i++) if ((x[i] ?? 0) !== (y[i] ?? 0)) return (x[i] ?? 0) > (y[i] ?? 0);
  return false;
}

function badge(c: LinkClient): { tone: StatusTone; label: string } {
  if (c.state === 2) return { tone: 'info', label: '업데이트 중' };
  if (c.requested) return { tone: 'info', label: '업데이트 요청함' };
  if (c.latest && newer(c.latest, c.current)) return { tone: 'warning', label: `새 버전 ${c.latest}` };
  return { tone: 'success', label: '최신' };
}

function seenText(c: LinkClient): string {
  if (c.connected) return '지금 접속 중';
  if (!c.seen) return '';
  const minutes = Math.round((Date.now() / 1000 - c.seen) / 60);
  if (minutes < 1) return '방금 전';
  if (minutes < 60) return `${minutes}분 전`;
  const hours = Math.round(minutes / 60);
  return hours < 48 ? `${hours}시간 전` : `${Math.round(hours / 24)}일 전`;
}

async function updateClient(c: LinkClient): Promise<void> {
  busy.value = { ...busy.value, [c.uuid]: true };
  try {
    await apiPost('/api/apollide/update-client', { uuid: c.uuid });
    await load();
  } finally {
    busy.value = { ...busy.value, [c.uuid]: false };
  }
}

onMounted(() => {
  void load();
  timer = setInterval(() => void load(), 5000);
});
onBeforeUnmount(() => clearInterval(timer));
</script>

<template>
  <section v-if="clients.length" class="apollide-devices" aria-labelledby="apollide-devices-title">
    <div class="apollide-devices__heading">
      <h2 id="apollide-devices-title">연결한 기기</h2>
      <p>새 버전이 나오면 여기서 바로 업데이트해요 · 접속 중이던 화면은 업데이트 뒤 다시 이어져요</p>
    </div>
    <TransitionGroup name="apollide-row" tag="ul" class="apollide-devices__list">
      <li v-for="c in clients" :key="c.uuid" class="apollide-device">
        <span class="apollide-device__icon" :class="{ 'is-live': c.connected }"><UiIcon name="devices" :size="20" /></span>
        <span class="apollide-device__copy">
          <strong>{{ c.name || '이름 없는 기기' }}</strong>
          <span>Artemide {{ c.current || '?' }} · {{ seenText(c) }}</span>
        </span>
        <Transition name="apollide-badge" mode="out-in">
          <StatusBadge :key="badge(c).label" :tone="badge(c).tone" :label="badge(c).label" compact />
        </Transition>
        <AppButton
          v-if="c.latest && newer(c.latest, c.current) && c.state !== 2 && !c.requested"
          label="업데이트"
          variant="secondary"
          :busy="busy[c.uuid]"
          busy-label="요청하는 중"
          @click="updateClient(c)"
        />
      </li>
    </TransitionGroup>
  </section>
</template>

<style scoped>
.apollide-devices {
  padding: var(--vs-space-24);
  border: var(--vs-border-width) solid var(--vs-color-border-subtle);
  border-radius: var(--vs-radius-card);
  background: var(--vs-color-bg-surface);
}
.apollide-devices__heading h2 {
  font-size: 16px;
  line-height: 24px;
}
.apollide-devices__heading p {
  margin-top: var(--vs-space-4);
  color: var(--vs-color-text-muted);
}
.apollide-devices__list {
  margin: var(--vs-space-12) 0 0;
  padding: 0;
  list-style: none;
}
.apollide-device {
  display: flex;
  align-items: center;
  gap: var(--vs-space-16);
  padding: var(--vs-space-16) 0;
}
.apollide-device + .apollide-device {
  border-top: 1px solid var(--vs-color-border-subtle);
}
.apollide-device__icon {
  position: relative;
  display: grid;
  flex: none;
  width: 40px;
  height: 40px;
  place-items: center;
  background: var(--vs-color-bg-subtle);
  border-radius: var(--vs-radius-control);
  color: var(--vs-color-text-secondary);
}
/* A live client breathes softly, like the host's own status dot */
.apollide-device__icon.is-live::after {
  position: absolute;
  top: 4px;
  right: 4px;
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: var(--vs-color-status-success);
  content: '';
  animation: apollide-breathe 2.4s ease-in-out infinite;
}
@keyframes apollide-breathe {
  50% {
    opacity: 0.45;
    transform: scale(0.8);
  }
}
.apollide-device__copy {
  display: grid;
  flex: 1;
  min-width: 0;
}
.apollide-device__copy span {
  color: var(--vs-color-text-muted);
  font-size: 13px;
}
.apollide-row-enter-active,
.apollide-row-leave-active {
  transition:
    opacity 200ms cubic-bezier(0.23, 1, 0.32, 1),
    transform 200ms cubic-bezier(0.23, 1, 0.32, 1);
}
.apollide-row-enter-from,
.apollide-row-leave-to {
  opacity: 0;
  transform: translateY(6px);
}
.apollide-badge-enter-active,
.apollide-badge-leave-active {
  transition:
    opacity 150ms ease-out,
    transform 150ms ease-out;
}
.apollide-badge-enter-from {
  opacity: 0;
  transform: scale(0.96);
}
.apollide-badge-leave-to {
  opacity: 0;
}
@media (prefers-reduced-motion: reduce) {
  .apollide-device__icon.is-live::after {
    animation: none;
  }
  .apollide-row-enter-active,
  .apollide-row-leave-active {
    transition: opacity 120ms linear;
  }
  .apollide-row-enter-from,
  .apollide-row-leave-to {
    transform: none;
  }
}
</style>
