#!/usr/bin/env python3
"""Adapts a copy of the VST3 SDK 3.8 so the plug-in cross-compiles with llvm-mingw:
   - no VSTGUI standalone library (it has no MinGW sources);
   - Win32 task executor without the Microsoft PPL (std::thread instead);
   - aligned_alloc through _aligned_malloc;
   - DirectComposition stubbed (MinGW headers lack the newer interfaces; VSTGUI falls back
     to plain Direct2D).
Usage: patch_vst3sdk_mingw.py <copy of vst3sdk>
Then build with -DVST3_SDK_ROOT=<copy> -DSMTG_RUN_VST_VALIDATOR=OFF -DSMTG_CREATE_MODULE_INFO=OFF
and CMAKE_CXX_FLAGS="-DVSTGUI_WIN32_CUSTOMFONT_SUPPORT=0 -D__maybenull= -D__out= -D__in=
-DWS_EX_NOREDIRECTIONBITMAP=0x00200000L" (an 'attrib' no-op command must be in PATH)."""
import sys, os, shutil
root = sys.argv[1]
def edit(rel, old, new, count=1):
    p = os.path.join(root, rel); s = open(p).read()
    if new and new in s: return
    if not new and old not in s: return
    assert s.count(old) >= 1, (rel, old[:40]); s = s.replace(old, new); open(p, 'w').write(s)
edit('cmake/modules/SMTG_VstGuiSupport.cmake', '    set(VSTGUI_STANDALONE ON)\n',
     '    if(NOT MINGW)\n        set(VSTGUI_STANDALONE ON)\n    endif()\n')
edit('public.sdk/source/vst/utility/alignedalloc.h', '#elif defined(_MSC_VER)', '#elif defined(_WIN32)')
te = 'vstgui4/vstgui/lib/platform/win32/win32taskexecutor.cpp'
edit(te, '#include <ppltasks.h>\n', '')
edit(te, '''	void schedule ()
	{
		f = std::make_shared<concurrency::task<void>> ([This = shared_from_this ()] () {
			This->task ();
			This->f = nullptr;
		});
	}

	Tasks::Task task;
	std::shared_ptr<concurrency::task<void>> f;''', '''	void schedule ()
	{
		std::thread ([This = shared_from_this ()] () { This->task (); }).detach ();
	}

	Tasks::Task task;''')
p = os.path.join(root, te); s = open(p).read()
if 'concurrency::task<void> ctask' in s:
    a = s.index('	void schedule (Tasks::Task&& task) const override\n	{\n		auto t = [f = std::move (task), queue = shared_from_this ()]')
    b = s.index('	mutable std::mutex mutex;\n};', a)
    s = s[:a] + '''	void schedule (Tasks::Task&& task) const override
	{
		// MinGW has no PPL: one detached worker drains the queue in order
		numTasks++;
		std::lock_guard<std::mutex> guard (mutex);
		pending.push_back (std::move (task));
		if (running)
			return;
		running = true;
		auto self = std::const_pointer_cast<SerialQueue> (shared_from_this ());
		std::thread ([self] () {
			for (;;)
			{
				Tasks::Task t;
				{
					std::lock_guard<std::mutex> g (self->mutex);
					if (self->pending.empty ())
					{
						self->running = false;
						return;
					}
					t = std::move (self->pending.front ());
					self->pending.erase (self->pending.begin ());
				}
				t ();
				self->numTasks--;
			}
		}).detach ();
	}

private:
	mutable bool running {false};
	std::string name;
	mutable std::vector<Tasks::Task> pending;
''' + s[b:]
    open(p, 'w').write(s)
shutil.copy(os.path.join(os.path.dirname(__file__), '..', 'plugin', 'mingw_dcomp_stub.cpp'),
            os.path.join(root, 'vstgui4/vstgui/lib/platform/win32/win32directcomposition.cpp'))
print('ok')
