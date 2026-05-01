"""
@file build_frontend.py
@brief PlatformIO Extra Script for automating the frontend build.

Ensures the React frontend is compiled into static assets before 
LittleFS image creation (buildfs/uploadfs).
"""

Import("env")
import subprocess
import os

def build_frontend(source, target, env):
    """
    Executes the npm build command in the frontend directory.
    """
    print("--------------------------------------------------")
    print("AUTOMATION: Building React frontend...")
    print("--------------------------------------------------")
    
    # PROJECT_DIR is the 'backend' folder
    project_dir = env.get("PROJECT_DIR")
    frontend_dir = os.path.join(project_dir, "..", "frontend")
    
    try:
        # We use subprocess.run with check=True to catch build failures.
        # This requires Node.js and npm to be in the system PATH.
        subprocess.run(["npm", "run", "build"], cwd=frontend_dir, check=True)
        print("--------------------------------------------------")
        print("SUCCESS: Frontend assets updated in /backend/data")
        print("--------------------------------------------------")
    except Exception as e:
        print("--------------------------------------------------")
        print(f"ERROR: Frontend build failed: {e}")
        print("Make sure Node.js is installed and 'npm install' was run in /frontend.")
        print("--------------------------------------------------")
        # Exit(1) could be used here to halt the PlatformIO build entirely on failure

# TRIGGER LOGIC:
# We check the COMMAND_LINE_TARGETS to see if the user is trying to build or upload the filesystem.
# If they are, we run the frontend build immediately. This ensures the /data folder is 
# refreshed BEFORE PlatformIO starts collecting files for mklittlefs.
if "buildfs" in COMMAND_LINE_TARGETS or "uploadfs" in COMMAND_LINE_TARGETS:
    build_frontend(None, None, env)
