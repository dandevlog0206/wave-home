import { render, screen } from '@testing-library/react';
import App from './App';

test('renders WaveHome dashboard headline', () => {
  render(<App />);
  const headline = screen.getByText(/파도에 몸을 맡기듯 당신의 집이 편안하도록, WaveHome/i);
  expect(headline).toBeInTheDocument();
});
