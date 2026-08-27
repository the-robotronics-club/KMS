"""Email templates for all notification types."""
from datetime import datetime


def retrieval_confirmation_html(user_name: str, room_name: str, due_at: datetime) -> str:
    due_str = due_at.strftime("%B %d, %Y at %I:%M %p")
    return f"""
<!DOCTYPE html>
<html>
<head><meta charset="UTF-8"></head>
<body style="font-family:Arial,sans-serif;max-width:600px;margin:auto;padding:20px;">
  <div style="background:#1a1a2e;color:white;padding:20px;border-radius:8px;">
    <h1 style="margin:0;font-size:22px;">🔑 Key Retrieved Successfully</h1>
  </div>
  <div style="padding:20px;border:1px solid #eee;border-radius:0 0 8px 8px;">
    <p>Hi <strong>{user_name}</strong>,</p>
    <p>You have successfully retrieved the key for <strong>{room_name}</strong>.</p>
    <div style="background:#f0f9ff;border-left:4px solid #0ea5e9;padding:12px;margin:16px 0;">
      <strong>Return by:</strong> {due_str}
    </div>
    <p>Please return the key within 6 hours. You can extend your possession window from the SNTC web app.</p>
    <p>If you need to return early, please log in and select "Return Key".</p>
    <hr style="margin:20px 0;">
    <small style="color:#666;">IIT Mandi SAC — Smart Key Storage System</small>
  </div>
</body>
</html>"""


def return_reminder_html(user_name: str, room_name: str, due_at: datetime) -> str:
    due_str = due_at.strftime("%B %d, %Y at %I:%M %p")
    return f"""
<!DOCTYPE html>
<html>
<body style="font-family:Arial,sans-serif;max-width:600px;margin:auto;padding:20px;">
  <div style="background:#f59e0b;color:white;padding:20px;border-radius:8px;">
    <h1 style="margin:0;font-size:22px;">⏰ Key Return Reminder</h1>
  </div>
  <div style="padding:20px;border:1px solid #eee;border-radius:0 0 8px 8px;">
    <p>Hi <strong>{user_name}</strong>,</p>
    <p>This is a reminder that the key for <strong>{room_name}</strong> is due back in <strong>30 minutes</strong>.</p>
    <div style="background:#fffbeb;border-left:4px solid #f59e0b;padding:12px;margin:16px 0;">
      <strong>Due at:</strong> {due_str}
    </div>
    <p>
      <a href="#" style="background:#0ea5e9;color:white;padding:10px 20px;border-radius:6px;text-decoration:none;">
        Extend My Time
      </a>
    </p>
    <p>Or return the key by visiting the enclosure and connecting to the enclosure WiFi.</p>
    <hr style="margin:20px 0;">
    <small style="color:#666;">IIT Mandi SAC — Smart Key Storage System</small>
  </div>
</body>
</html>"""


def overdue_warning_html(user_name: str, room_name: str) -> str:
    return f"""
<!DOCTYPE html>
<html>
<body style="font-family:Arial,sans-serif;max-width:600px;margin:auto;padding:20px;">
  <div style="background:#dc2626;color:white;padding:20px;border-radius:8px;">
    <h1 style="margin:0;font-size:22px;">🚨 Key Overdue</h1>
  </div>
  <div style="padding:20px;border:1px solid #eee;border-radius:0 0 8px 8px;">
    <p>Hi <strong>{user_name}</strong>,</p>
    <p>The key for <strong>{room_name}</strong> is now <strong>overdue</strong>. Please return it immediately.</p>
    <p>If you need more time, please extend your possession window from the SNTC app.</p>
    <p>Failure to return the key may result in your access being suspended.</p>
    <hr style="margin:20px 0;">
    <small style="color:#666;">IIT Mandi SAC — Smart Key Storage System</small>
  </div>
</body>
</html>"""


def coordinator_escalation_html(
    coordinator_name: str, member_name: str, member_email: str, room_name: str, retrieved_at: datetime
) -> str:
    retrieved_str = retrieved_at.strftime("%B %d, %Y at %I:%M %p")
    return f"""
<!DOCTYPE html>
<html>
<body style="font-family:Arial,sans-serif;max-width:600px;margin:auto;padding:20px;">
  <div style="background:#7c3aed;color:white;padding:20px;border-radius:8px;">
    <h1 style="margin:0;font-size:22px;">⚠️ Key Overdue — Escalation</h1>
  </div>
  <div style="padding:20px;border:1px solid #eee;border-radius:0 0 8px 8px;">
    <p>Hi <strong>{coordinator_name}</strong>,</p>
    <p>The key for <strong>{room_name}</strong> has not been returned and requires your attention.</p>
    <div style="background:#f3f0ff;border-left:4px solid #7c3aed;padding:12px;margin:16px 0;">
      <strong>Member:</strong> {member_name} ({member_email})<br>
      <strong>Retrieved at:</strong> {retrieved_str}<br>
      <strong>Status:</strong> Overdue — 2+ hours past due
    </div>
    <p>Please follow up with this member directly.</p>
    <hr style="margin:20px 0;">
    <small style="color:#666;">IIT Mandi SAC — Smart Key Storage System</small>
  </div>
</body>
</html>"""


def tamper_alert_html(device_name: str, ts: datetime) -> str:
    ts_str = ts.strftime("%B %d, %Y at %I:%M %p")
    return f"""
<!DOCTYPE html>
<html>
<body style="font-family:Arial,sans-serif;max-width:600px;margin:auto;padding:20px;">
  <div style="background:#1e1e2e;color:#ff4444;padding:20px;border-radius:8px;border:2px solid #ff4444;">
    <h1 style="margin:0;font-size:22px;">🔓 ALERT: Physical Override Detected</h1>
  </div>
  <div style="padding:20px;border:1px solid #ff4444;border-radius:0 0 8px 8px;">
    <p>The outer box on device <strong>{device_name}</strong> was opened at <strong>{ts_str}</strong>.</p>
    <p>This is an emergency physical access event. Please log in to the admin panel and add a resolution note.</p>
    <p style="color:#dc2626;font-weight:bold;">Action required: Resolve this tamper event in the dashboard.</p>
    <hr style="margin:20px 0;">
    <small style="color:#666;">IIT Mandi SAC — Smart Key Storage System</small>
  </div>
</body>
</html>"""
