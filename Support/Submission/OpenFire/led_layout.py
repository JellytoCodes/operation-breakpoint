"""Single Square layout shared by the installation guide and detection view.

Ratios match OpenFIRE-App d0f3334 appcali.cpp modeAlignment: 30/70 percent
for aspect >1.4, 20/80 percent otherwise. Numbers are physical row-major IDs.
"""
LED_NAMES = ('1 상단 왼쪽', '2 상단 오른쪽', '3 하단 왼쪽', '4 하단 오른쪽')
OUTLINE_ORDER = (0, 1, 3, 2, 0)


def square_targets(width, height):
    if width <= 0 or height <= 0:
        raise ValueError('Display dimensions must be positive')
    left, right = (.3, .7) if width/height > 1.4 else (.2, .8)
    return ((left*width, 0.), (right*width, 0.),
            (left*width, float(height)), (right*width, float(height)))


def draw_alignment_guide(canvas):
    """Native display coordinates; markers meet the top/bottom screen edges."""
    canvas.delete('all')
    width, height = max(1, canvas.winfo_width()), max(1, canvas.winfo_height())
    targets = square_targets(width, height)
    scale = min(width/1920, height/1080)
    font = max(11, int(20*scale))
    small = max(9, int(14*scale))
    margin = max(12, int(24*scale))
    half = max(16, int(35*scale))
    canvas.create_rectangle(0, 0, width, height, fill='#101923', outline='', tags='guide_background')
    for x in sorted({p[0] for p in targets}):
        canvas.create_line(x, 0, x, height, fill='#293c4e', dash=(6, 7), tags='guide_column')
    for i, (x, y) in enumerate(targets):
        bottom = i >= 2
        inside = height-half if bottom else half
        canvas.create_rectangle(x-half, y-half, x+half, y+half,
                                fill='#8b3039', outline='#ff858d', width=3,
                                tags=('guide_target', f'guide_target_{i}'))
        canvas.create_line(x-half-12, y, x+half+12, y, fill='#ffbac0', width=2)
        canvas.create_line(x, y-half-12, x, y+half+12, fill='#ffbac0', width=2)
        percent = round(x/width*100)
        canvas.create_text(x, inside+(-margin if bottom else margin),
                           anchor='s' if bottom else 'n', text=f'{LED_NAMES[i]} · 가로 {percent}%',
                           fill='#ffe0e2', font=('맑은 고딕', font, 'bold'), tags='guide_label')
    canvas.create_text(width/2, height*.38, text='IR LED 설치 가이드', fill='#e5edf5',
                       font=('맑은 고딕', max(14, int(32*scale)), 'bold'), tags='guide_title')
    canvas.create_text(width/2, height*.46, text='Square · 상단 2개 / 하단 2개', fill='#49d6b0',
                       font=('맑은 고딕', font, 'bold'))
    canvas.create_text(width/2, height*.55,
                       text='빨간 표식의 가로 위치에 LED 중심을 맞춰\n화면 위·아래 테두리 바깥에 부착하세요.\n위아래의 왼쪽 LED끼리, 오른쪽 LED끼리 같은 세로선에 맞춥니다.',
                       fill='#bac9dc', font=('맑은 고딕', small), justify='center',
                       width=max(100, width*.8), tags='guide_instructions')
    canvas.create_text(width/2, height*.72,
                       text='선택한 화면 전체에 표시됩니다.  ·  Esc 또는 화면 클릭: 닫기',
                       fill='#9baec0', font=('맑은 고딕', small), tags='guide_exit')
    return targets
