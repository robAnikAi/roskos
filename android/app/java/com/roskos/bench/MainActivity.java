package com.roskos.bench;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.util.Locale;

/**
 * roSkos Donut Bench.
 *
 * Режимы:
 *   1 · Vulkan      — прямое Vulkan-приложение;
 *   2 · OpenGL      — GLES, объект за объектом, матрицы в uniform;
 *   3 · roSkos      — те же шейдеры и геометрия, но один инстансный вызов;
 *   4 · Мир         — блоки мира, вызов на каждый блок, смена uniform;
 *   5 · Мир батч    — тот же мир и то же отсечение, один инстансный вызов.
 *
 * Сравнение 2↔3 и 4↔5 честное: одинаковая сцена, одинаковая геометрия,
 * одинаковые шейдеры. Отличается только способ отправки на GPU.
 *
 * Переключатели сверху:
 *   vsync: включён — потолок равен частоте экрана; выключен — потолок GPU;
 *   пиксели: 1×1 / 2×2 / 4×4 — рендер в уменьшенный буфер и апскейл (и LOD);
 *   свет: вершины / пиксели — где считать освещение (одинаково для 2 и 3).
 *
 * FPS считается по реальному времени кадров в нативном потоке.
 */
public class MainActivity extends Activity implements SurfaceHolder.Callback {

    static {
        System.loadLibrary("roskosbench");
    }

    private static native boolean nativeStart(Surface surface, int backend, int width, int height);
    private static native void nativeStop();
    private static native void nativeResize(int width, int height);
    private static native float nativeFps();
    private static native float nativeFpsAverage();
    private static native float nativeFrameMs();
    private static native String nativeStatus();
    private static native void nativeSetVsync(boolean on);
    private static native boolean nativeVsync();
    private static native void nativeSetCoarse(int scale);
    private static native int nativeCoarse();
    private static native void nativeSetVertexLit(boolean on);
    private static native boolean nativeVertexLit();

    private static final String[] BACKEND_LABELS = {
            "Vulkan", "OpenGL", "roSkos", "Мир (по блокам)", "Мир (батч)"
    };
    private static final int[] ROW_ONE = {0, 1, 2};
    private static final int[] ROW_TWO = {3, 4};

    private SurfaceView surfaceView;
    private SurfaceHolder holder;
    private TextView fpsView;
    private TextView statusView;
    private TextView vsyncView;
    private TextView coarseView;
    private TextView lightView;

    private final Button[] backendButtons = new Button[BACKEND_LABELS.length];

    private int backend = 1;
    private boolean vsync = true;
    private int coarse = 1;
    private boolean vertexLit = true;
    private boolean surfaceReady = false;
    private boolean resumed = false;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable statsTick = new Runnable() {
        @Override
        public void run() {
            updateStats();
            handler.postDelayed(this, 250);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.parseColor("#0B0F14"));

        surfaceView = new SurfaceView(this);
        holder = surfaceView.getHolder();
        holder.addCallback(this);
        root.addView(surfaceView, new FrameLayout.LayoutParams(-1, -1));

        fpsView = new TextView(this);
        fpsView.setTextColor(Color.WHITE);
        fpsView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 44f);
        fpsView.setShadowLayer(10f, 0f, 2f, Color.BLACK);
        fpsView.setText("— FPS");
        FrameLayout.LayoutParams fpsParams =
                new FrameLayout.LayoutParams(-2, -2, Gravity.TOP | Gravity.START);
        fpsParams.setMargins(dp(20), dp(16), dp(20), 0);
        root.addView(fpsView, fpsParams);

        statusView = new TextView(this);
        statusView.setTextColor(Color.parseColor("#9FB3C8"));
        statusView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f);
        statusView.setShadowLayer(8f, 0f, 2f, Color.BLACK);
        statusView.setText("запуск…");
        FrameLayout.LayoutParams statusParams =
                new FrameLayout.LayoutParams(-2, -2, Gravity.TOP | Gravity.START);
        statusParams.setMargins(dp(22), dp(76), dp(20), 0);
        root.addView(statusView, statusParams);

        LinearLayout chips = new LinearLayout(this);
        chips.setOrientation(LinearLayout.HORIZONTAL);
        chips.setGravity(Gravity.END);

        vsyncView = makeChip();
        vsyncView.setOnClickListener(new android.view.View.OnClickListener() {
            @Override
            public void onClick(android.view.View view) {
                setVsync(!vsync);
            }
        });
        chips.addView(vsyncView);

        coarseView = makeChip();
        coarseView.setOnClickListener(new android.view.View.OnClickListener() {
            @Override
            public void onClick(android.view.View view) {
                setCoarse(coarse >= 4 ? 1 : (coarse >= 2 ? 4 : 2));
            }
        });
        LinearLayout.LayoutParams chipParams = new LinearLayout.LayoutParams(-2, -2);
        chipParams.leftMargin = dp(8);
        chips.addView(coarseView, chipParams);

        lightView = makeChip();
        lightView.setOnClickListener(new android.view.View.OnClickListener() {
            @Override
            public void onClick(android.view.View view) {
                setVertexLit(!vertexLit);
            }
        });
        chips.addView(lightView, chipParams);

        FrameLayout.LayoutParams chipsParams =
                new FrameLayout.LayoutParams(-2, -2, Gravity.TOP | Gravity.END);
        chipsParams.setMargins(dp(20), dp(18), dp(20), 0);
        root.addView(chips, chipsParams);

        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(10), 0, dp(10), dp(14));

        panel.addView(makeRow(ROW_ONE));
        LinearLayout.LayoutParams rowGap = new LinearLayout.LayoutParams(-1, -2);
        rowGap.topMargin = dp(8);
        panel.addView(makeRow(ROW_TWO), rowGap);

        FrameLayout.LayoutParams panelParams =
                new FrameLayout.LayoutParams(-1, -2, Gravity.BOTTOM);
        root.addView(panel, panelParams);

        setContentView(root);

        vsync = nativeVsync();
        coarse = nativeCoarse();
        vertexLit = nativeVertexLit();
        updateChips();
        selectBackend(backend);
    }

    private LinearLayout makeRow(int[] indices) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        for (int index : indices) {
            Button button = new Button(this);
            button.setText(BACKEND_LABELS[index]);
            button.setAllCaps(false);
            button.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f);
            button.setPadding(dp(2), dp(10), dp(2), dp(10));
            button.setMinWidth(0);
            button.setMinimumWidth(0);
            button.setStateListAnimator(null);
            button.setOnClickListener(new android.view.View.OnClickListener() {
                @Override
                public void onClick(android.view.View view) {
                    selectBackend(index);
                }
            });
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, -2, 1f);
            params.setMargins(dp(4), 0, dp(4), 0);
            row.addView(button, params);
            backendButtons[index] = button;
        }
        return row;
    }

    private TextView makeChip() {
        TextView chip = new TextView(this);
        chip.setTextColor(Color.parseColor("#E6EEF8"));
        chip.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f);
        chip.setBackgroundColor(Color.parseColor("#26404E"));
        chip.setPadding(dp(10), dp(6), dp(10), dp(6));
        return chip;
    }

    private void selectBackend(int index) {
        backend = index;
        for (int i = 0; i < backendButtons.length; i++) {
            Button button = backendButtons[i];
            if (button == null) continue;
            boolean selected = i == index;
            button.setBackgroundColor(selected ? Color.parseColor("#3D7EBB")
                                               : Color.parseColor("#1B2530"));
            button.setTextColor(selected ? Color.WHITE : Color.parseColor("#CFD8E3"));
        }
        if (surfaceReady) startBackend();
    }

    private void setVsync(boolean on) {
        vsync = on;
        nativeSetVsync(on);
        updateChips();
        if (surfaceReady && resumed) startBackend();
    }

    private void setCoarse(int scale) {
        coarse = scale;
        nativeSetCoarse(scale);
        updateChips();
        if (surfaceReady && resumed) startBackend();
    }

    private void setVertexLit(boolean on) {
        vertexLit = on;
        nativeSetVertexLit(on);
        updateChips();
        if (surfaceReady && resumed) startBackend();
    }

    private void updateChips() {
        vsyncView.setText(vsync ? "vsync: ВКЛ" : "vsync: ВЫКЛ");
        vsyncView.setTextColor(vsync ? Color.parseColor("#E6EEF8") : Color.parseColor("#FFD479"));
        coarseView.setText("пиксели: " + (coarse <= 1 ? "1×1" : (coarse == 2 ? "2×2" : "4×4")));
        coarseView.setTextColor(coarse <= 1 ? Color.parseColor("#E6EEF8") : Color.parseColor("#FFD479"));
        lightView.setText(vertexLit ? "свет: вершины" : "свет: пиксели");
    }

    private void startBackend() {
        Surface surface = holder.getSurface();
        if (surface == null || !surface.isValid()) return;
        nativeStop();
        nativeSetVsync(vsync);
        nativeSetCoarse(coarse);
        nativeSetVertexLit(vertexLit);
        int width = Math.max(1, surfaceView.getWidth());
        int height = Math.max(1, surfaceView.getHeight());
        if (!nativeStart(surface, backend, width, height)) {
            statusView.setText(nativeStatus());
        }
    }

    private void updateStats() {
        float fps = nativeFps();
        float frameMs = nativeFrameMs();
        float average = nativeFpsAverage();
        fpsView.setText(String.format(Locale.US, "%.1f FPS", fps));
        statusView.setText(String.format(Locale.US, "%s · %.2f мс · средний %.1f",
                nativeStatus(), frameMs, average));
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumed = true;
        if (surfaceReady) startBackend();
        handler.post(statsTick);
    }

    @Override
    protected void onPause() {
        super.onPause();
        resumed = false;
        handler.removeCallbacks(statsTick);
        nativeStop();
    }

    @Override
    public void surfaceCreated(SurfaceHolder surfaceHolder) {
        surfaceReady = true;
        if (resumed) startBackend();
    }

    @Override
    public void surfaceChanged(SurfaceHolder surfaceHolder, int format, int width, int height) {
        nativeResize(width, height);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder surfaceHolder) {
        surfaceReady = false;
        nativeStop();
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
