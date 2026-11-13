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
 * roSkos Donut Bench: три бэкенда, один и тот же пончик, счётчик FPS.
 *
 * FPS считается по реальному времени между кадрами в нативном потоке: ничего
 * не дорисовывается и не подменяется. При включённом vsync потолок — частота
 * экрана (60/120 Гц), поэтому есть переключатель vsync для честного замера
 * потолка GPU.
 *
 * Второй переключатель — «грубые пиксели»: сцена рисуется в буфер в 2 или 4
 * раза меньший по каждой оси и растягивается на экран. Это в 4 и 16 раз меньше
 * работы фрагментного шейдера, но картинка мягче.
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

    private static final String[] BACKEND_LABELS = {"1 · Vulkan", "2 · OpenGL", "3 · roSkos"};

    private SurfaceView surfaceView;
    private SurfaceHolder holder;
    private TextView fpsView;
    private TextView statusView;
    private TextView vsyncView;
    private TextView coarseView;
    private int backend = 1;
    private boolean vsync = true;
    private int coarse = 1;
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
        fpsParams.setMargins(dp(20), dp(18), dp(20), 0);
        root.addView(fpsView, fpsParams);

        statusView = new TextView(this);
        statusView.setTextColor(Color.parseColor("#B8C7DA"));
        statusView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f);
        statusView.setShadowLayer(8f, 0f, 2f, Color.BLACK);
        FrameLayout.LayoutParams statusParams =
                new FrameLayout.LayoutParams(-2, -2, Gravity.TOP | Gravity.START);
        statusParams.setMargins(dp(22), dp(76), dp(20), 0);
        root.addView(statusView, statusParams);

        LinearLayout chips = new LinearLayout(this);
        chips.setOrientation(LinearLayout.VERTICAL);

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
        chipParams.topMargin = dp(6);
        chips.addView(coarseView, chipParams);

        FrameLayout.LayoutParams chipsParams =
                new FrameLayout.LayoutParams(-2, -2, Gravity.TOP | Gravity.START);
        chipsParams.setMargins(dp(22), dp(100), dp(20), 0);
        root.addView(chips, chipsParams);

        LinearLayout buttons = new LinearLayout(this);
        buttons.setOrientation(LinearLayout.HORIZONTAL);
        buttons.setGravity(Gravity.CENTER);
        buttons.setPadding(dp(10), dp(10), dp(10), dp(26));

        for (int i = 0; i < BACKEND_LABELS.length; i++) {
            final int index = i;
            Button button = new Button(this);
            button.setText(BACKEND_LABELS[i]);
            button.setAllCaps(false);
            button.setOnClickListener(new android.view.View.OnClickListener() {
                @Override
                public void onClick(android.view.View view) {
                    selectBackend(index);
                }
            });
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, -2, 1f);
            params.setMargins(dp(5), 0, dp(5), 0);
            buttons.addView(button, params);
        }

        FrameLayout.LayoutParams buttonsParams =
                new FrameLayout.LayoutParams(-1, -2, Gravity.BOTTOM);
        root.addView(buttons, buttonsParams);

        setContentView(root);

        vsync = nativeVsync();
        coarse = nativeCoarse();
        updateVsyncLabel();
        updateCoarseLabel();
        selectBackend(backend);
    }

    private TextView makeChip() {
        TextView chip = new TextView(this);
        chip.setTextColor(Color.parseColor("#E6EEF8"));
        chip.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f);
        chip.setBackgroundColor(Color.parseColor("#33FFFFFF"));
        chip.setPadding(dp(10), dp(6), dp(10), dp(6));
        return chip;
    }

    private void selectBackend(int index) {
        backend = index;
        if (surfaceReady) startBackend();
        statusView.setText("Бэкенд: " + BACKEND_LABELS[index] + " · замер…");
    }

    private void setVsync(boolean on) {
        vsync = on;
        nativeSetVsync(on);
        updateVsyncLabel();
        // режим презентации выбирается при создании бэкенда — перезапускаем его
        if (surfaceReady && resumed) startBackend();
    }

    private void setCoarse(int scale) {
        coarse = scale;
        nativeSetCoarse(scale);
        updateCoarseLabel();
        if (surfaceReady && resumed) startBackend();
    }

    private void updateVsyncLabel() {
        if (vsync) {
            vsyncView.setText("vsync: ВКЛ · потолок = частота экрана (нажмите, чтобы снять)");
        } else {
            vsyncView.setText("vsync: ВЫКЛ · реальный потолок GPU (нажмите, чтобы вернуть)");
        }
    }

    private void updateCoarseLabel() {
        if (coarse <= 1) {
            coarseView.setText("пиксели: 1×1 · полное разрешение (нажмите: 2×2)");
        } else if (coarse == 2) {
            coarseView.setText("пиксели: 2×2 · в 4 раза меньше работы (нажмите: 4×4)");
        } else {
            coarseView.setText("пиксели: 4×4 · в 16 раз меньше работы (нажмите: 1×1)");
        }
    }

    private void startBackend() {
        Surface surface = holder.getSurface();
        if (surface == null || !surface.isValid()) return;
        nativeStop();
        nativeSetVsync(vsync);
        nativeSetCoarse(coarse);
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
        String status = nativeStatus();
        statusView.setText(String.format(Locale.US, "%s · %.2f мс · средний %.1f FPS",
                status, frameMs, average));
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
