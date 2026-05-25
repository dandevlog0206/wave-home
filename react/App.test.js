import { render, screen } from '@testing-library/react';
import App from './App';

test('renders WaveHome dashboard headline', () => {
  render(<App />);
  const headline = screen.getByText(/WaveHome/i);
  expect(headline).toBeInTheDocument();
});
