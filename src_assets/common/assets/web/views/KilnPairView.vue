<script setup lang="ts">
// Apollide: one-tap Kiln link — Kiln's [H1 화면 연결] opens this page; the person checks the four
// digits against Kiln and approves. The key (nine scopes, see kiln_pair_math.h) goes straight to Kiln.
import { computed, onBeforeUnmount, onMounted, ref } from 'vue';

import { ApiError, apiGet, apiPost } from '@/api/client';
import { AppButton, InlineAlert, PageHeader, UiIcon } from '@/components/ui';

interface PendingResponse {
  pending?: boolean;
  code?: string;
  expires_in?: number;
}

type Phase = 'loading' | 'waiting' | 'pending' | 'approved' | 'denied';

const phase = ref<Phase>('loading');
const code = ref('');
const secondsLeft = ref(0);
const busy = ref<'' | 'approve' | 'deny'>('');
const error = ref('');

const digits = computed(() => code.value.split(''));
const countdown = computed(() => {
  const s = Math.max(0, secondsLeft.value);
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
});

let pollTimer: number | undefined;
let tickTimer: number | undefined;

async function refresh(): Promise<void> {
  if (phase.value === 'approved' || phase.value === 'denied' || busy.value) return;
  try {
    const r = await apiGet<PendingResponse>('/api/kiln/pair');
    if (r.pending && r.code) {
      code.value = r.code;
      secondsLeft.value = r.expires_in ?? 0;
      phase.value = 'pending';
    } else {
      phase.value = 'waiting';
    }
  } catch {
    if (phase.value === 'loading') phase.value = 'waiting';
  }
}

async function answer(kind: 'approve' | 'deny'): Promise<void> {
  error.value = '';
  busy.value = kind;
  try {
    await apiPost(`/api/kiln/pair/${kind}`, { code: code.value });
    phase.value = kind === 'approve' ? 'approved' : 'denied';
  } catch (cause) {
    error.value =
      cause instanceof ApiError
        ? '이 요청은 이미 끝났거나 숫자가 달라요. Kiln에서 [H1 화면 연결]을 다시 눌러 주세요.'
        : '답을 보내지 못했어요. 잠시 뒤 다시 눌러 주세요.';
    phase.value = 'waiting';
  } finally {
    busy.value = '';
  }
}

onMounted(() => {
  void refresh();
  pollTimer = window.setInterval(() => void refresh(), 2000);
  tickTimer = window.setInterval(() => {
    if (phase.value !== 'pending') return;
    secondsLeft.value -= 1;
    if (secondsLeft.value <= 0) phase.value = 'waiting';
  }, 1000);
});

onBeforeUnmount(() => {
  window.clearInterval(pollTimer);
  window.clearInterval(tickTimer);
});
</script>

<template>
  <div class="vs-page vs-page--settings kiln-pair-page">
    <PageHeader
      title="Kiln 연결 허락"
      description="Kiln이 이 PC(H1)의 화면을 브라우저로 보고, 작업용 모니터를 켜고 끌 수 있게 해요."
    />

    <section class="kiln-card vs-surface" aria-live="polite">
      <Transition name="kiln-swap" mode="out-in">
        <div v-if="phase === 'loading'" key="loading" class="kiln-state">
          <div class="kiln-skeleton" aria-hidden="true" />
          <p class="kiln-muted">요청을 확인하는 중…</p>
        </div>

        <div v-else-if="phase === 'waiting'" key="waiting" class="kiln-state">
          <div class="kiln-icon" aria-hidden="true"><UiIcon name="devices" :size="22" /></div>
          <h2>기다리는 연결 요청이 없어요</h2>
          <p class="kiln-muted">Kiln 대화창 [창] → H1 화면에서 [H1 화면 연결]을 누르면 여기에 바로 나타나요.</p>
          <InlineAlert v-if="error" tone="danger" title="허락하지 못했어요" announce="assertive">
            {{ error }}
          </InlineAlert>
        </div>

        <div v-else-if="phase === 'pending'" key="pending" class="kiln-state">
          <div class="kiln-icon" aria-hidden="true"><UiIcon name="key" :size="22" /></div>
          <h2>Kiln이 H1 화면을 보려고 해요</h2>
          <p class="kiln-muted">Kiln 화면에 보이는 숫자와 같을 때만 허락해 주세요.</p>

          <div class="kiln-code" role="group" :aria-label="`확인 숫자 ${code}`">
            <span
              v-for="(d, i) in digits"
              :key="`${code}-${i}`"
              class="kiln-digit"
              :style="{ animationDelay: `${i * 50}ms` }"
            >{{ d }}</span>
          </div>

          <ul class="kiln-scope">
            <li><UiIcon name="check" :size="16" aria-hidden="true" /> H1 화면을 브라우저로 보고 조작하기</li>
            <li><UiIcon name="check" :size="16" aria-hidden="true" /> Kiln 작업용 모니터 켜고 끄기</li>
            <li class="kiln-scope__no"><UiIcon name="x" :size="16" aria-hidden="true" /> 설정 바꾸기·열쇠 만들기는 못 해요</li>
          </ul>

          <div class="kiln-actions">
            <AppButton
              variant="primary"
              label="허락"
              icon="check"
              :busy="busy === 'approve'"
              busy-label="허락하는 중…"
              :disabled="!!busy"
              @click="answer('approve')"
            />
            <AppButton
              variant="secondary"
              label="거절"
              :busy="busy === 'deny'"
              busy-label="거절하는 중…"
              :disabled="!!busy"
              @click="answer('deny')"
            />
          </div>
          <p class="kiln-timer">{{ countdown }} 안에 답하지 않으면 요청이 사라져요</p>
        </div>

        <div v-else-if="phase === 'approved'" key="approved" class="kiln-state">
          <svg class="kiln-done" viewBox="0 0 52 52" aria-hidden="true">
            <circle class="kiln-done__ring" cx="26" cy="26" r="23" />
            <path class="kiln-done__check" d="M15 27l7 7 15-16" />
          </svg>
          <h2>연결했어요</h2>
          <p class="kiln-muted">Kiln으로 돌아가면 H1 화면이 보여요. 이 탭은 닫아도 돼요.</p>
        </div>

        <div v-else key="denied" class="kiln-state">
          <div class="kiln-icon kiln-icon--muted" aria-hidden="true"><UiIcon name="x" :size="22" /></div>
          <h2>거절했어요</h2>
          <p class="kiln-muted">Kiln은 열쇠를 받지 못했어요. 필요하면 Kiln에서 다시 요청하면 돼요.</p>
        </div>
      </Transition>
    </section>
  </div>
</template>

<style scoped>
.kiln-pair-page {
  display: grid;
  gap: var(--vs-space-24);
}

.kiln-card {
  max-width: 34rem;
  padding: var(--vs-space-24);
}

.kiln-state {
  display: grid;
  justify-items: center;
  gap: var(--vs-space-12);
  text-align: center;
}

.kiln-state h2 {
  font-size: var(--vs-type-size-section);
  line-height: var(--vs-type-line-height-section);
}

.kiln-muted,
.kiln-timer {
  color: var(--vs-color-text-secondary);
  font-size: var(--vs-type-size-metadata);
  line-height: var(--vs-type-line-height-metadata);
}

.kiln-timer {
  font-variant-numeric: tabular-nums;
}

.kiln-icon {
  display: grid;
  width: 44px;
  height: 44px;
  place-items: center;
  border-radius: var(--vs-radius-control);
  background: var(--vs-color-bg-subtle);
  color: var(--vs-color-accent-default);
}

.kiln-icon--muted {
  color: var(--vs-color-text-secondary);
}

.kiln-code {
  display: flex;
  gap: var(--vs-space-8);
  margin: var(--vs-space-8) 0;
}

.kiln-digit {
  display: grid;
  width: 3rem;
  height: 3.5rem;
  place-items: center;
  border: 1px solid var(--vs-color-border-strong);
  border-radius: var(--vs-radius-control);
  color: var(--vs-color-text-primary);
  font-family: var(--vs-type-family-mono);
  font-size: 1.75rem;
  font-variant-numeric: tabular-nums;
  animation: kiln-digit-in 260ms var(--vs-motion-easing-standard) both;
}

@keyframes kiln-digit-in {
  from {
    opacity: 0;
    transform: translateY(6px) scale(0.96);
  }
}

.kiln-scope {
  display: grid;
  gap: var(--vs-space-8);
  padding: var(--vs-space-12) var(--vs-space-16);
  margin: 0;
  border-radius: var(--vs-radius-control);
  background: var(--vs-color-bg-subtle);
  list-style: none;
  text-align: start;
  font-size: var(--vs-type-size-metadata);
}

.kiln-scope li {
  display: flex;
  align-items: center;
  gap: var(--vs-space-8);
}

.kiln-scope li > svg {
  flex: none;
  color: var(--vs-color-status-success);
}

.kiln-scope__no {
  color: var(--vs-color-text-secondary);
}

.kiln-scope__no > svg {
  color: var(--vs-color-text-secondary) !important;
}

.kiln-actions {
  display: flex;
  gap: var(--vs-space-12);
  margin-top: var(--vs-space-8);
}

.kiln-actions :deep(.vs-button) {
  min-width: 7rem;
}

.kiln-skeleton {
  width: 12rem;
  height: 3.5rem;
  border-radius: var(--vs-radius-control);
  background: var(--vs-color-bg-subtle);
  animation: kiln-pulse 1.2s ease-in-out infinite alternate;
}

@keyframes kiln-pulse {
  to {
    opacity: 0.5;
  }
}

.kiln-done {
  width: 56px;
  height: 56px;
  fill: none;
  stroke: var(--vs-color-status-success);
  stroke-linecap: round;
  stroke-linejoin: round;
  stroke-width: 3;
}

.kiln-done__ring {
  stroke-dasharray: 145;
  stroke-dashoffset: 145;
  animation: kiln-draw 420ms var(--vs-motion-easing-standard) forwards;
}

.kiln-done__check {
  stroke-dasharray: 34;
  stroke-dashoffset: 34;
  animation: kiln-draw 260ms var(--vs-motion-easing-standard) 300ms forwards;
}

@keyframes kiln-draw {
  to {
    stroke-dashoffset: 0;
  }
}

.kiln-swap-enter-active {
  transition:
    opacity 240ms var(--vs-motion-easing-standard),
    transform 240ms var(--vs-motion-easing-standard);
}

.kiln-swap-leave-active {
  transition:
    opacity 140ms var(--vs-motion-easing-exit),
    transform 140ms var(--vs-motion-easing-exit);
}

.kiln-swap-enter-from {
  opacity: 0;
  transform: translateY(6px) scale(0.98);
}

.kiln-swap-leave-to {
  opacity: 0;
  transform: scale(0.98);
}

@media (max-width: 767px) {
  .kiln-card {
    padding: var(--vs-space-16);
  }

  .kiln-actions {
    width: 100%;
  }

  .kiln-actions > * {
    flex: 1;
  }
}

@media (prefers-reduced-motion: reduce) {
  .kiln-digit,
  .kiln-skeleton {
    animation: none;
  }

  .kiln-done__ring,
  .kiln-done__check {
    animation: none;
    stroke-dashoffset: 0;
  }

  .kiln-swap-enter-active,
  .kiln-swap-leave-active {
    transition: opacity 120ms linear;
  }

  .kiln-swap-enter-from,
  .kiln-swap-leave-to {
    transform: none;
  }
}
</style>
