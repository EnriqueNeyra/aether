document.addEventListener('DOMContentLoaded', () => {
  // Intersection Observer for scroll animations
  const revealElements = document.querySelectorAll('.reveal');

  const revealOptions = {
    threshold: 0.1,
    rootMargin: "0px 0px -50px 0px"
  };

  const revealOnScroll = new IntersectionObserver(function(entries, observer) {
    entries.forEach(entry => {
      if (!entry.isIntersecting) {
        return;
      } else {
        entry.target.classList.add('active');
        // Stop observing once revealed
        observer.unobserve(entry.target);
      }
    });
  }, revealOptions);

  revealElements.forEach(el => {
    revealOnScroll.observe(el);
  });
  
  // Also trigger active for elements already in view on load
  setTimeout(() => {
    revealElements.forEach(el => {
      const rect = el.getBoundingClientRect();
      if (rect.top <= window.innerHeight) {
        el.classList.add('active');
      }
    });
  }, 100);
});

  // Tab switching logic
  const tabBtns = document.querySelectorAll('.tab-btn');
  const tabPanes = document.querySelectorAll('.tab-pane');

  tabBtns.forEach(btn => {
    btn.addEventListener('click', () => {
      // Remove active from all
      tabBtns.forEach(b => b.classList.remove('active'));
      tabPanes.forEach(p => p.classList.remove('active'));

      // Add active to current
      btn.classList.add('active');
      const targetId = `tab-${btn.dataset.tab}`;
      document.getElementById(targetId).classList.add('active');
    });
  });

  // Mobile detection for WebUSB warning
  const isMobile = /iPhone|iPad|iPod|Android/i.test(navigator.userAgent);
  if (isMobile) {
    document.getElementById('mobile-warning').style.display = 'block';
  }
