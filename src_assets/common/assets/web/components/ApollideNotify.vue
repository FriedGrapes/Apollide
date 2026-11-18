<script setup lang="ts">
// Apollide: the 'send H1 notifications to the Surface' switch and what happened to the
// latest one. See src/apollide/notify_relay.h.
import { computed, onBeforeUnmount, onMounted, ref } from 'vue';
import { apiGet, apiPost } from '@/api/client';
import { StatusBadge } from '@/components/ui';
import type { StatusTone } from '@/components/ui/types';

interface NotifySettings {
  enabled: boolean;
  helper: number; // 0 not running yet, 1 ok, 2 blocked in Windows settings
  localUse: boolean;
  streaming: boolean;
  last?: { app: string; title: string; at: number; sent: boolean; why: string };
}

const settings = ref<NotifySettings | null>(null);
const saving = ref(false);
let timer: ReturnType<typeof setInterval> | undefined;

async function load(): Promise<void> {
  try {
    settings.value = await apiGet<NotifySettings>('/api/apollide/notify');
  } catch {
    // The service restarts while it updates; the next poll picks it up again
  }
}

async function toggle(event: Event): Promise<void> {
  const enabled = (event.target as HTMLInputElement).checked;
  if (settings.value) settings.value = { ...settings.value, enabled };
  saving.value = true;
  try {
    settings.value = await apiPost<NotifySettings>('/api/apollide/notify', { enabled });
  } finally {
    saving.value = false;
  }
}

const badge = computed<{ tone: StatusTone; label: string }>(() => {
  const s = settings.value;
  if (!s || !s.enabled) return { tone: 'neutral', label: '꺼 둠' };
  if (s.helper === 2) return { tone: 'warning', label: '윈도우에서 막힘' };
  if (s.helper !== 1) return { tone: 'neutral', label: '준비 중' };
  if (s.streaming) return { tone: 'success', label: '접속 중 · 보내는 중' };
  if (s.localUse) return { tone: 'info', label: 'H1 사용 중 · 쉬는 중' };
  return { tone: 'success', label: '보내는 중' };
});

function ago(at: number): string {
  const minutes = Math.round((Date.now() / 1000 - at) / 60);
  if (minutes < 1) return '방금 전';
  if (minutes < 60) return `${minutes}분 전`;
  const hours = Math.round(minutes / 60);
  return hours < 48 ? `${hours}시간 전` : `${Math.round(hours / 24)}일 전`;
}

const lastText = computed(() => {
  const last = settings.value?.last;
  if (!last) return '';
  const what = [last.app, last.title].filter(Boolean).join(' · ');
  const result = last.sent
    ? '보냈어요'
    : last.why === 'local'
      ? 'H1을 직접 쓰는 중이라 보내지 않았어요'
      : '꺼 둬서 보내지 않았어요';
  return `마지막 알림: ${what} — ${ago(last.at)} ${result}`;
});

onMounted(() => {
  void load();
  timer = setInterval(() => void load(), 5000);
});
onBeforeUnmount(() => clearInterval(timer));
</script>

<template>
  <section class="apollide-notify" aria-labelledby="apollide-notify-title">
    <div class="apollide-notify__row">
      <div class="apollide-notify__heading">
        <h2 id="apollide-notify-title">서피스로 알림 보내기</h2>
        <p>
          H1에 뜨는 윈도우 알림을 Artemide 기기로 보내요 · 접속하지 않았을 때 H1 앞에서 직접 쓰고
          있으면 쉬어요
        </p>
      </div>
      <Transition name="apollide-badge" mode="out-in">
        <StatusBadge :key="badge.label" :tone="badge.tone" :label="badge.label" compact />
      </Transition>
      <label class="vs-switch">
        <input
          id="apollide-notify-enabled"
          type="checkbox"
          :checked="settings?.enabled ?? true"
          :disabled="!settings || saving"
          @change="toggle"
        />
        <span class="vs-switch__track" aria-hidden="true" />
        <span class="vs-sr-only">서피스로 알림 보내기</span>
      </label>
    </div>
    <Transition name="apollide-note" mode="out-in">
      <p
        v-if="settings?.enabled && settings.helper === 2"
        key="denied"
        class="apollide-notify__note is-warning"
      >
        윈도우 설정 › 개인 정보 및 보안 › 알림에서 '앱이 알림에 액세스하도록 허용'을 켜 주세요.
      </p>
      <p v-else-if="lastText" key="last" class="apollide-notify__note">{{ lastText }}</p>
    </Transition>
  </section>
</template>

<style scoped>
.apollide-notify {
  padding: var(--vs-space-24);
  border: var(--vs-border-width) solid var(--vs-color-border-subtle);
  border-radius: var(--vs-radius-card);
  background: var(--vs-color-bg-surface);
}
.apollide-notify__row {
  display: flex;
  align-items: center;
  gap: var(--vs-space-16);
}
.apollide-notify__heading {
  flex: 1;
  min-width: 0;
}
.apollide-notify__heading h2 {
  font-size: 16px;
  line-height: 24px;
}
.apollide-notify__heading p {
  margin-top: var(--vs-space-4);
  color: var(--vs-color-text-muted);
  word-break: keep-all;
}
.apollide-notify__note {
  margin-top: var(--vs-space-12);
  padding-top: var(--vs-space-12);
  border-top: 1px solid var(--vs-color-border-subtle);
  color: var(--vs-color-text-muted);
  font-size: 13px;
  overflow-wrap: anywhere;
  word-break: keep-all;
}
.apollide-notify__note.is-warning {
  color: var(--vs-color-status-warning);
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
.apollide-note-enter-active,
.apollide-note-leave-active {
  transition:
    opacity 200ms cubic-bezier(0.23, 1, 0.32, 1),
    transform 200ms cubic-bezier(0.23, 1, 0.32, 1);
}
.apollide-note-enter-from,
.apollide-note-leave-to {
  opacity: 0;
  transform: translateY(-4px);
}
@media (max-width: 640px) {
  .apollide-notify__row {
    flex-wrap: wrap;
  }
  .apollide-notify__heading {
    flex-basis: 100%;
  }
}
@media (prefers-reduced-motion: reduce) {
  .apollide-note-enter-from,
  .apollide-note-leave-to {
    transform: none;
  }
}
</style>
